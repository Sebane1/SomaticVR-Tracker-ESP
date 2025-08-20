/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2022 TheDevMinerTV

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#include "LEDManager.h"

#include "GlobalVars.h"
#include "status/Status.h"

#define LED_RAMP_MILLIS 50

#define LEDC_MAX ((1u<<m_ledcBits)-1)
#define MAX_BRIGHTNESS 0.10f // in percent


namespace SlimeVR
{
    void LEDManager::setup()
    {
#if ENABLE_LEDS
#if ESP32 && ENABLE_LEDC
        ledcAttachChannel(m_Pin, m_ledcFrequency, m_ledcBits, 5);  // define the PWM Setup
#else
        pinMode(m_Pin, OUTPUT);
#endif
#endif

        // Do the initial pull of the state
        update();
    }

    void LEDManager::on()
    {
#if ENABLE_LEDS
#if ESP32 && ENABLE_LEDC
        setBrightness((unsigned int) LEDC_MAX);
#else
        digitalWrite(m_Pin, LED__ON);
#endif
#endif
    }

    void LEDManager::off()
    {
#if ENABLE_LEDS
#if ESP32 && ENABLE_LEDC
        setBrightness((unsigned int)0);
#else
        digitalWrite(m_Pin, LED__OFF);
#endif
#endif
    }

    void LEDManager::blink(unsigned long time)
    {
        on();
        delay(time);
        off();
    }

    void LEDManager::pattern(unsigned long timeon, unsigned long timeoff, int times)
    {
        for (int i = 0; i < times; i++)
        {
            blink(timeon);
            delay(timeoff);
        }
    }

#if ESP32 && ENABLE_LEDC
    void LEDManager::setBrightness(float percent)
    {        
        setBrightness( (unsigned int)(percent/100.0f * LEDC_MAX));
    }

    void LEDManager::setBrightness(unsigned int brightness)
    {       
        if (brightness > LEDC_MAX)
            brightness = LEDC_MAX;
        if (brightness == m_CurrentBrightness)
            return;
        m_CurrentBrightness = brightness;
#if LED_INVERTED
        // m_Logger.trace("Brightness set to %d. max is %d", LEDC_MAX - brightness, LEDC_MAX);
        ledcWrite(m_Pin, LEDC_MAX - brightness * MAX_BRIGHTNESS);
#else
        // m_Logger.trace("Brightness set to %d. max is %d", brightness, LEDC_MAX);
        ledcWrite(m_Pin, brightness * MAX_BRIGHTNESS);
#endif        
    }

    void LEDManager::setRamp(float startPercent, float endPercent, unsigned long ms)
    {
        m_CurrentStage = RAMP;
        setRamp((unsigned int)(startPercent/100.0f * LEDC_MAX), (unsigned int)(endPercent/100.0f * LEDC_MAX), ms);
    }

    void LEDManager::setRamp(unsigned int startBrightness, unsigned int endBrightness, unsigned long ms)
    {
        m_CurrentStage = RAMP;
        m_rampStartBrightness = startBrightness;
        m_rampEndBrightness = endBrightness;
        m_rampDifference = ((int)m_rampEndBrightness - (int)m_rampStartBrightness) / (ms/LED_RAMP_MILLIS);
        if (m_rampDifference > m_rampEndBrightness)
            m_rampDifference = m_rampEndBrightness;
        // m_Logger.trace("Ramp Start: %d, end: %d, ramp: %d", startBrightness, endBrightness, m_rampDifference);
        setBrightness(m_rampStartBrightness);
    }

    void LEDManager::rampFromCurrent(float endPercent, unsigned long ms)
    {
        rampFromCurrent((unsigned int)(endPercent/100.0f * LEDC_MAX), ms);
    }

    void LEDManager::rampFromCurrent(unsigned int endBrightness, unsigned long ms)
    {
        setRamp(m_CurrentBrightness, endBrightness, ms);
    }
#endif

    void LEDManager::update()
    {
        unsigned long time = millis();
        unsigned long diff = time - m_LastUpdate;

        // Don't tick the LEDManager *too* often
        if (diff < 10)
        {
            return;
        }
        m_LastUpdate = time;

        unsigned int length = 0;
        unsigned int count = 0;
        if (statusManager.hasStatus(SlimeVR::Status::SHUTDOWN))
        {
            count = SHUTDOWN_COUNT;
            switch (m_CurrentStage)
            {
            case ON:
            case OFF:
                length = SHUTDOWN_LENGTH;
                break;
            case GAP:
                length = DEFAULT_GAP;
                break;
            case INTERVAL:
                length = SHUTDOWN_INTERVAL;
                break;
            case RAMP:
            case RAMP_CONTINUOUS:
                m_CurrentStage = RAMP;
                rampFromCurrent(0.0f,1000);
                break;
            }
        }
        else if (statusManager.hasStatus(Status::BATTERY_CHARGE_COMPLETE))
        {
            // m_Logger.info("Battery Charge Complete: %d", m_CurrentStage);
            switch (m_CurrentStage)
            {
            case ON:
            case OFF:
            case INTERVAL:
            case RAMP:
            case RAMP_CONTINUOUS:
#if ESP32 && ENABLE_LEDC
                rampFromCurrent(50.0f, 1000);
                length = LED_RAMP_MILLIS;
#endif
                break;
            }
        }
        else if (statusManager.hasStatus(Status::BATTERY_CHARGING))
        {
            // m_Logger.info("Battery Charging: %d", m_CurrentStage);
#if ESP32 && ENABLE_LEDC 
            switch (m_CurrentStage)
            {
            case ON:
            case OFF:
            case INTERVAL:
                setRamp(10.0f, 100.0f, 2000);
                length = LED_RAMP_MILLIS;
            case RAMP:
                m_CurrentStage = RAMP_CONTINUOUS;
            case RAMP_CONTINUOUS:
                length = LED_RAMP_MILLIS;
                break;
            }
#endif
        }
        else if (statusManager.hasStatus(Status::LOW_BATTERY))
        {
            count = LOW_BATTERY_COUNT;
            switch (m_CurrentStage)
            {
            case ON:
            case OFF:
                length = LOW_BATTERY_LENGTH;
                break;
            case GAP:
                length = DEFAULT_GAP;
                break;
            case INTERVAL:
                length = LOW_BATTERY_INTERVAL;
                break;
            case RAMP:
            case RAMP_CONTINUOUS:
                m_CurrentStage = RAMP;
                rampFromCurrent(0.0f,1000);
                break;
            }
        }
        else if (statusManager.hasStatus(Status::IMU_ERROR))
        {
            count = IMU_ERROR_COUNT;
            switch (m_CurrentStage)
            {
            case ON:
            case OFF:
                length = IMU_ERROR_LENGTH;
                break;
            case GAP:
                length = DEFAULT_GAP;
                break;
            case INTERVAL:
                length = IMU_ERROR_INTERVAL;
                break;
            case RAMP:
            case RAMP_CONTINUOUS:
                m_CurrentStage = RAMP;
                rampFromCurrent(0.0f,1000);
                break;
            }
        }
        else if (statusManager.hasStatus(Status::WIFI_CONNECTING))
        {
            count = WIFI_CONNECTING_COUNT;
            switch (m_CurrentStage)
            {
            case ON:
            case OFF:
                length = WIFI_CONNECTING_LENGTH;
                break;
            case GAP:
                length = DEFAULT_GAP;
                break;
            case INTERVAL:
                length = WIFI_CONNECTING_INTERVAL;
                break;
            case RAMP:
            case RAMP_CONTINUOUS:
                m_CurrentStage = RAMP;
                rampFromCurrent(0.0f,1000);
                break;
            }
        }
        else if (statusManager.hasStatus(Status::SERVER_CONNECTING))
        {
            count = SERVER_CONNECTING_COUNT;
            switch (m_CurrentStage)
            {
            case ON:
            case OFF:
                length = SERVER_CONNECTING_LENGTH;
                break;
            case GAP:
                length = DEFAULT_GAP;
                break;
            case INTERVAL:
                length = SERVER_CONNECTING_INTERVAL;
                break;
            case RAMP:
            case RAMP_CONTINUOUS:
                m_CurrentStage = RAMP;
                rampFromCurrent(0.0f,500);
                break;
            }
        }
        // else if (statusManager.hasStatus(Status::SERVER_SEARCHING))
        // {
        //     count = SERVER_SEARCHING_COUNT;
        //     switch (m_CurrentStage)
        //     {
        //     case ON:
        //     case OFF:
        //         length = SERVER_SEARCHING_LENGTH;
        //         break;
        //     case GAP:
        //         length = DEFAULT_GAP;
        //         break;
        //     case INTERVAL:
        //         length = SERVER_SEARCHING_INTERVAL;
        //         break;
        //     case RAMP:
        //     case RAMP_CONTINUOUS:
        //         m_CurrentStage = RAMP;
        //         rampFromCurrent(0.0f,500);
        //         break;
        //     }
        // }
        else
        {
#if defined(LED_INTERVAL_STANDBY) && LED_INTERVAL_STANDBY > 0
            count = 1;
            // m_Logger.info("default: %d", m_CurrentStage);
            switch (m_CurrentStage)
            {
            case ON:
            case OFF:
                length = STANDBY_LENGTH;
                break;
            case GAP:
                length = DEFAULT_GAP;
                break;
            case INTERVAL:
                length = LED_INTERVAL_STANDBY;
                break;
            case RAMP:
            case RAMP_CONTINUOUS:
                m_CurrentStage = RAMP;
                rampFromCurrent(0.0f,500);
                break;
            }
#else
            return;
#endif
        }
        

        if (m_CurrentStage == OFF || m_Timer + diff >= length)
        {
            m_Timer = 0;
            // Advance stage
            switch (m_CurrentStage)
            {
            case OFF:
                on();
                m_CurrentStage = ON;
                m_CurrentCount = 0;
                break;
            case ON:
                off();
                m_CurrentCount++;
                if (m_CurrentCount >= count)
                {
                    m_CurrentCount = 0;
                    m_CurrentStage = INTERVAL;
                }
                else
                {
                    m_CurrentStage = GAP;
                }
                break;
            case GAP:
            case INTERVAL:
                on();
                m_CurrentStage = ON;
                break;
            case RAMP:
#if ESP32 && ENABLE_LEDC
                // m_Logger.trace("RAMP: %d, %d, %d, %d", m_CurrentBrightness, m_rampDifference, m_rampStartBrightness, m_rampEndBrightness);
                setBrightness(m_CurrentBrightness + m_rampDifference);
                if (((m_rampDifference > 0) && (m_CurrentBrightness >= m_rampEndBrightness)) || 
                    ((m_rampDifference < 0) && (m_CurrentBrightness <= -m_rampDifference)) || (m_rampDifference == 0))
                    m_CurrentStage = (m_CurrentBrightness > 0)?ON:OFF;
                break;
            case RAMP_CONTINUOUS:
                // m_Logger.trace("RAMP CONT: %d, %d, %d, %d", m_CurrentBrightness, m_rampDifference, m_rampStartBrightness, m_rampEndBrightness);
                setBrightness(m_CurrentBrightness + m_rampDifference);
                if (((m_rampDifference > 0) && (m_CurrentBrightness >= m_rampEndBrightness)) || 
                    ((m_rampDifference < 0) && (m_CurrentBrightness <= -m_rampDifference)) || (m_rampDifference == 0))
                {
                    unsigned int temp = m_rampStartBrightness;
                    m_rampStartBrightness = m_rampEndBrightness;
                    m_rampEndBrightness = temp;
                    m_rampDifference = - m_rampDifference;
                }
#endif
                break;
            }
        }
        else
        {
            m_Timer += diff;
        }
    }
}  // namespace SlimeVR
