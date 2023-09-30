/*
 * This example turns the ESP32 into a Bluetooth LE gamepad that presses buttons and moves axis
 *
 * At the moment we are using the default settings, but they can be canged using a BleGamepadConfig instance as parameter for the begin function.
 *
 * Possible buttons are:
 * BUTTON_1 through to BUTTON_16
 * (16 buttons by default. Library can be configured to use up to 128)
 *
 * Possible DPAD/HAT switch position values are:
 * DPAD_CENTERED, DPAD_UP, DPAD_UP_RIGHT, DPAD_RIGHT, DPAD_DOWN_RIGHT, DPAD_DOWN, DPAD_DOWN_LEFT, DPAD_LEFT, DPAD_UP_LEFT
 * (or HAT_CENTERED, HAT_UP etc)
 *
 * bleGamepad.setAxes sets all axes at once. There are a few:
 * (x axis, y axis, z axis, rx axis, ry axis, rz axis, slider 1, slider 2)
 *
 * Library can also be configured to support up to 5 simulation controls
 * (rudder, throttle, accelerator, brake, steering), but they are not enabled by default.
 *
 * Library can also be configured to support different function buttons
 * (start, select, menu, home, back, volume increase, volume decrease, volume mute)
 * start and select are enabled by default
 */

#include <Arduino.h>
#include <BleGamepad.h>
#include <Bounce2.h>
#include <Keypad.h>
#include "AiEsp32RotaryEncoder.h"

#define ROW_NUM 4
#define COLUMN_NUM  4

#define CLK 21
#define DT 22
#define ROTARY_ENCODER_STEPS 4

#define numOfButtons 7

char keys[ROW_NUM][COLUMN_NUM] = {
  {1, 8, 13, 15},
  {5, 2, 9, 10},
  {11, 6, 3, 14},
  {16, 12, 7, 4}
};

byte pin_rows[ROW_NUM] = {2, 0, 17, 16};
byte pin_column[COLUMN_NUM] = {27, 25, 32, 4};

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM);


Bounce debouncers[numOfButtons];
byte buttonPins[numOfButtons] = {26, 5, 23, 33, 19, 18, 14};
byte physicalButtons[numOfButtons] = {1, 2, 3, 4, 5, 6, 7};

BleGamepad bleGamepad("Center console", "eCrowne", 100);
BleGamepadConfiguration bleGamepadConfig;

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(DT, CLK, -1, ROTARY_ENCODER_STEPS);

// do you have more than 64 buttons? if so, this will overflow
uint64_t holding = 0B0;

void IRAM_ATTR readEncoderISR()
{
    rotaryEncoder.readEncoder_ISR();
}

void setup()
{
    pinMode(CLK, INPUT_PULLUP);
    pinMode(DT, INPUT_PULLUP);
    rotaryEncoder.areEncoderPinsPulldownforEsp32=false;
    rotaryEncoder.begin();
    rotaryEncoder.setup(readEncoderISR);
    rotaryEncoder.setBoundaries(0, 8190, false);
    rotaryEncoder.setAcceleration(250);

    for (byte currentPinIndex = 0; currentPinIndex < numOfButtons; currentPinIndex++)
    {
        pinMode(buttonPins[currentPinIndex], INPUT_PULLUP);

        debouncers[currentPinIndex] = Bounce();
        debouncers[currentPinIndex].attach(buttonPins[currentPinIndex]); // After setting up the button, setup the Bounce instance :
        debouncers[currentPinIndex].interval(10);
    }

    Serial.println("Starting BLE work!");
    bleGamepadConfig.setAutoReport(false);
    bleGamepadConfig.setAxesMax(32760);
    bleGamepadConfig.setIncludeSlider1(true);
    bleGamepadConfig.setIncludeXAxis(false);
    bleGamepadConfig.setIncludeYAxis(false);
    bleGamepadConfig.setIncludeZAxis(false);
    bleGamepadConfig.setIncludeRxAxis(false);
    bleGamepadConfig.setIncludeRyAxis(false);
    bleGamepadConfig.setIncludeRzAxis(false);
    bleGamepadConfig.setButtonCount(numOfButtons + ROW_NUM * COLUMN_NUM);
    bleGamepad.begin(&bleGamepadConfig);
    Serial.begin(115200);
    keypad.setDebounceTime(10);
    keypad.setHoldTime(700);
}


void loop()
{
    if (bleGamepad.isConnected())
    {
        bool sendReport = false;

        for (byte currentIndex = 0; currentIndex < numOfButtons; currentIndex++)
        {
            debouncers[currentIndex].update();

            if (debouncers[currentIndex].fell())
            {
                bleGamepad.press(physicalButtons[currentIndex]);
                sendReport = true;
                Serial.print("Button ");
                Serial.print(physicalButtons[currentIndex]);
                Serial.println(" pressed.");
            }
            else if (debouncers[currentIndex].rose())
            {
                bleGamepad.release(physicalButtons[currentIndex]);
                sendReport = true;
                Serial.print("Button ");
                Serial.print(physicalButtons[currentIndex]);
                Serial.println(" released.");
            }
        }

        if (keypad.getKeys())
        {
            for (int i=0; i<LIST_MAX; i++)
            {
                if (keypad.key[i].stateChanged)
                {
                    bool wasHolding;
                    char matrixCode = keypad.key[i].kchar;
                    char buttonCode = matrixCode + numOfButtons;
                    switch (keypad.key[i].kstate) {
                        case PRESSED:
                            bleGamepad.press(buttonCode);
                            sendReport = true;
                            Serial.print("Button ");
                            Serial.print(buttonCode, DEC);
                            Serial.println(" pressed.");
                            break;
                        case HOLD:                    
                            break;
                            // TODO: something different on hold?
                            holding |= 1 << (matrixCode - 1);
                            // handle hold action
                            Serial.print("Button ");
                            Serial.print(buttonCode, DEC);
                            Serial.println(" held.");
                            bleGamepad.release(buttonCode);
                            sendReport = true;
                            break;
                        case RELEASED:
                            wasHolding = (holding & 1 << (matrixCode -1));
                            if (wasHolding) {
                                holding ^= 1 << (matrixCode -1);
                                // this was held, action should be handled by hold handler
                            } else {
                                // we should handle this here
                                Serial.print("Button ");
                                Serial.print(buttonCode, DEC);
                                Serial.println(" released.");
                                bleGamepad.release(buttonCode);
                                sendReport = true;
                            }
                            break;
                        default:
                            // nothing
                            break;
                    }
                }
            }
        }

        if (rotaryEncoder.encoderChanged())
        {
            bleGamepad.setSlider1(rotaryEncoder.readEncoder() * 4);
            sendReport = true;
            Serial.println(rotaryEncoder.readEncoder() * 4);
        }

        if (sendReport)
        {
            bleGamepad.sendReport();
        }
    }
}