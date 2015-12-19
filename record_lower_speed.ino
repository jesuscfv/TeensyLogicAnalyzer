/* Teensy Logic Analyzer
 * Copyright (c) 2015 LAtimes2
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

void recordLowSpeedData (struct sumpVariableStruct &sv)
{
  enum stateType {
    Buffering,
    LookingForTrigger,
    Triggered
  };

  register uint32_t *inputPtr = (uint32_t *)sv.startPtr;
  uint32_t *endOfBuffer = (uint32_t *)sv.endOfBuffer;
  uint32_t *startOfBuffer = (uint32_t *)sv.startOfBuffer;
  uint32_t *startPtr = (uint32_t *)sv.startPtr;
  register byte samplesPerElementMinusOne = (sv.samplesPerByte * 4) - 1;
  register uint32_t sampleMask = sv.sampleMask;
  register uint32_t sampleShift = sv.sampleShift;
  uint32_t *triggerPtr = startOfBuffer;
  int triggerCount = -1;     // -1 means trigger hasn't occurred yet
  register int workingCount = samplesPerElementMinusOne + 1;
  register uint32_t workingValue = 0;

  stateType state = Buffering;

  // if using a trigger
  if (sumpTrigMask)
  {
    state = Buffering;
  }
  else
  {
    state = Triggered;
  }

  // number of samples to delay before arming the trigger
  // (if not trigger, then this is 0)
  sv.delaySamples = sumpSamples - sumpDelaySamples;

  // add one due to truncation
  sv.delaySize = (sv.delaySamples / sv.samplesPerByte) + 1;

  // position to arm the trigger
  startPtr = inputPtr + sv.delaySize;

  // 100% causes a problem with circular buffer - never stops
  if (startPtr >= endOfBuffer)
  {
    startPtr = endOfBuffer - 1;
  }

  maskInterrupts ();

  // read enough samples prior to arming to meet the pre-trigger request
  // (for speed, use while (1) and break instead of while (inputPtr != startPtr))
  while (1)
  {
    waitForTimeout ();

    // read a sample
    workingValue = (workingValue << sampleShift) + (PORT_DATA_INPUT_REGISTER & sampleMask);
    --workingCount;

    if (workingCount == 0)
    {
      *(inputPtr) = workingValue;
      ++inputPtr;

      waitForTimeout ();

      // first value after saving workingValue doesn't need to
      // shift previous value nor mask off extra bits. This saves
      // time that was used saving workingValue above.
      workingValue = PORT_DATA_INPUT_REGISTER;
      workingCount = samplesPerElementMinusOne;

      // adjust for circular buffer wraparound at the end
      if (inputPtr >= endOfBuffer)
      {
        inputPtr = startOfBuffer;

        // if any data is received from PC, then stop (assume it is a reset)
        if (usbInterruptPending ())
        {
          DEBUG_SERIAL(print(" Halt due to USB interrupt"));
          set_led_off ();
          SUMPreset();
          break;
        }
      }
    }  // if workingCount == 0

    if (state == LookingForTrigger)
    {
      // if trigger has occurred
      if ((PORT_DATA_INPUT_REGISTER & sumpTrigMask) == sumpTrigValue)
      {
        triggerPtr = inputPtr;
        triggerCount = workingCount;
      
        // move to triggered state
        state = Triggered;
        set_led_off (); // TRIGGERED, turn off LED

        #ifdef TIMING_DISCRETES
          digitalWriteFast (TIMING_PIN_1, LOW);
        #endif
      }
    }
    else if (state == Triggered)
    {
      if (inputPtr == startPtr)
      {
        // done recording
        break;
      }
    }
    else if (state == Buffering)
    {
      // if enough data is buffered
      if (inputPtr >= startPtr)
      {
        // move to armed state
        state = LookingForTrigger;
        set_led_on ();

        #ifdef TIMING_DISCRETES
          digitalWriteFast (TIMING_PIN_1, HIGH);
        #endif
      }
    }

  } // while (1)

  // cleanup
  unmaskInterrupts ();

  // last location to save
  startPtr = triggerPtr - sv.delaySize;

  // adjust for circular buffer wraparound at the end.
  if (startPtr < startOfBuffer)
  {
    startPtr = startPtr + sv.bufferSize;
  }

  // adjust trigger count
  triggerCount = (samplesPerElementMinusOne + 1) - triggerCount;

  // send data back to SUMP client
  sv.startPtr = (byte *)startPtr;
  sv.triggerCount = triggerCount;

}






