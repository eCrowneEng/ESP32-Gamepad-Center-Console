/*
This configuration is for the "center console" project from eCrowne eng - https://discord.gg/aK2JKpwT4m

The gist of it is, there is a button matrix, an encoder and directly wired buttons (extra from the matrix)

We use an esp32 with bluetooth to emulate a Gamepad with (ROW_NUM * COLUMN_NUM + numOfButtons) total 
 buttons (the default case being 16 + 7 = 23). The matrix takes the first allocation of buttons from 1 to 16 for instance,
 and the directly wired ones take from 17 and on. If you define a matrix with 80 buttons, the directly wired ones will start from 81.

The encoder is sent as a "slider" in the gamepad, and is stateful until the device is reset.

*/

#include <Arduino.h>
#include <BleGamepad.h>
#include <Bounce2.h>
#include <Keypad.h>
#include "AiEsp32RotaryEncoder.h"

#define DEBUG false
// Matrix rows and column number
#define ROW_NUM 4
#define COLUMN_NUM  4
// extra direct pin buttons
#define numOfButtons 7
// encoder pin
#define CLK 21
#define DT 22
#define ROTARY_ENCODER_STEPS 4


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
//  it's only used for tracking held buttons, but currently that doesn't work
uint64_t holding = 0B0;

void IRAM_ATTR readEncoderISR()
{
    rotaryEncoder.readEncoder_ISR();
}

void setup()
{
    pinMode(CLK, INPUT_PULLUP);
    pinMode(DT, INPUT_PULLUP);
    rotaryEncoder.areEncoderPinsPulldownforEsp32 = false;
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
#if DEBUG
    Serial.begin(115200);
    Serial.println("Starting BLE work!");
#endif
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
#if DEBUG
                Serial.print("Button ");
                Serial.print(physicalButtons[currentIndex]);
                Serial.println(" pressed.");
#endif
            }
            else if (debouncers[currentIndex].rose())
            {
                bleGamepad.release(physicalButtons[currentIndex]);
                sendReport = true;
#if DEBUG
                Serial.print("Button ");
                Serial.print(physicalButtons[currentIndex]);
                Serial.println(" released.");
#endif
            }
        }
        yield;

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
#if DEBUG
                            Serial.print("Button ");
                            Serial.print(buttonCode, DEC);
                            Serial.println(" pressed.");
#endif
                            break;
                        case HOLD:
                            // NOTICE !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                            //  HOLD IS DISABLED BECAUSE OF THIS BREAK BELOW
                            //  also, unsure how to handle the previously sent "press" without a "release"
                            break;
                            holding |= 1 << (matrixCode - 1);
                            // handle hold action
                            // TODO: something different on hold?
                            bleGamepad.release(buttonCode);
                            sendReport = true;
#if DEBUG
                            Serial.print("Button ");
                            Serial.print(buttonCode, DEC);
                            Serial.println(" held.");
#endif
                            break;
                        case RELEASED:
                            wasHolding = (holding & 1 << (matrixCode -1));
                            if (wasHolding) {
                                holding ^= 1 << (matrixCode -1);
                                // this was held, action should be handled by hold handler
                            } else {
                                // we should handle this here
                                bleGamepad.release(buttonCode);
                                sendReport = true;
#if DEBUG
                                Serial.print("Button ");
                                Serial.print(buttonCode, DEC);
                                Serial.println(" released.");
#endif
                            }
                            break;
                        default:
                            // nothing
                            break;
                    }
                }
            }
        }
        yield;

        if (rotaryEncoder.encoderChanged())
        {
            long encoder = rotaryEncoder.readEncoder() * 4;
            bleGamepad.setSlider1(encoder);
            sendReport = true;
#if DEBUG
            Serial.print("encoder: ");
            Serial.println(encoder);
#endif
        }

        if (sendReport)
        {
            bleGamepad.sendReport();
        }
    }
}
