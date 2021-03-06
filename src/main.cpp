#include <Arduino.h>
#include <FastLED.h>

#include "GY_85.h"

#define LED_DATA_PIN     6
#define LED_COLOR_ORDER GRB
#define LED_CHIPSET     WS2812B
#define LED_COUNT 34

CRGB leds[LED_COUNT];

CRGBPalette16 palette(*PartyColors_p);

// this class is inspired / borrowed from https://github.com/sqrtmo/GY-85-arduino
// Wiring between the arduino and the the GY85 module is:
//    VCC_IN  ->   5V
//    GND     ->   GND
//    SCL     ->   A5 (clock line) - see https://www.arduino.cc/en/Reference/Wire
//    SDA     ->   A4 (data line)  - see https://www.arduino.cc/en/Reference/Wire
GY_85 GY85;

struct Pixel {
    byte value;
    byte color;
};

Pixel pixels[LED_COUNT];

float getAccelerometerChange();

float getMagnetometerHeading();

void setup() {
    // sanity check delay - allows reprogramming if accidentally blowing power w/leds
    delay(2000);

    FastLED.addLeds<LED_CHIPSET, LED_DATA_PIN, LED_COLOR_ORDER>(leds, LED_COUNT).setCorrection(TypicalLEDStrip);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 200);

    // 3 blink boot indicator
    for (byte i = 0; i < 4; ++i) {
        fill_solid(leds, LED_COUNT, CRGB::DeepPink);
        FastLED.delay(20);
        fill_solid(leds, LED_COUNT, CRGB::Black);
        FastLED.delay(50);
    }

    for (byte i = 0; i < LED_COUNT; i++) {
        pixels[i].value = 0;
        pixels[i].color = 0;
    }

    Wire.begin();
    Serial.begin(9600);
    GY85.begin();

    randomSeed((unsigned long) analogRead(0));
}

// @todo: add sleep mode with wake up from the GY85 interrupt(s)
void loop() {
    static unsigned long lastCheck = millis();
    static float last_change = 0;
    float accChange = 0;

    byte color = 0;

    unsigned long now = millis();

    // Read from the IMU once every 100ms
    if (now - lastCheck > 100) {
        lastCheck = now;
        // get the acceleration change in G (positive)
        accChange = getAccelerometerChange();
        // get the 360 direction relative from magnetic north and scale that to a
        // value that can be used with a CRGBPalette16
        color = (byte) (getMagnetometerHeading() / 360 * 255);
    }

    // Only trigger new LED'explosions' if the hat has a total acceleration more than 0.25G and
    // the change is rising
    if (accChange > 0.25 && accChange > last_change) {
        last_change = accChange;
        for (byte i = 0; i < 20; i++) {
            byte pixel = (byte) random(LED_COUNT);
            pixels[pixel].value = 255;
            pixels[pixel].color = (byte) (random(16) + color);
        }
    }

    if (accChange < 0.1) {
        last_change = 0;
    }

    for (byte i = 0; i < LED_COUNT; i++) {
        if (pixels[i].value < 3) {
            leds[i] = CRGB::Black;
            pixels[i].value = 0;
            continue;
        }
        leds[i] = ColorFromPalette(palette, pixels[i].color, pixels[i].value);
        pixels[i].value -= 3;
    }
    FastLED.show();
}

// get the total g force change (always positive) that is the sum of all axises.
// this is not strictly a real scientific value, but good enough for the use case.
float getAccelerometerChange() {
    static float filtered[3] = {0, 0, 0};
    static float prevFiltered[3] = {0, 0, 0};

    int *a = GY85.readFromAccelerometer();

    float reading[3] = {0, 0, 0};
    float change[3] = {0, 0, 0};
    float total_change = 0;

    // for each axis of the accelerometer (x,y,z)
    for (byte i = 0; i < 3; i++) {
        // convert to g force with the resolution of +/- 4G, see the GY_85::SetAccelerometer()
        // +/- 4G = Measurement Value * (2*4/(1024)) = 0.0078125
        reading[i] = *(i + a) * 0.0078125;

        // low-pass filter (0.5 is the alpha value)
        const float alpha = 0.5;
        filtered[i] = reading[i] * alpha + filtered[i] * (1.0 - alpha);

        // store the change since the last reading
        change[i] = prevFiltered[i] - filtered[i];
        prevFiltered[i] = filtered[i];

        // sum up the G force changes
        total_change += abs(change[i]);
    }
    return total_change;
}

// Get the compass direction (in deegres) from the the magnetometer. This function does
// not compensate if the sensor is not totally level with the ground.
float getMagnetometerHeading() {
    int *a = GY85.readFromCompass();
    for (byte i = 0; i < 3; i++) {
        *(i + a) *= 0.92;
    }

    // Calculate heading when the magnetometer is level, then correct for signs of axis.
    // Atan2() automatically check the correct formula taking care of the quadrant you are in
    float heading = atan2(*(1 + a), *(a));

    // Once you have your heading, you must then add your 'Declination Angle',
    // which is the 'Error' of the magnetic field in your location.
    // Find yours here: http://www.magnetic-declination.com/
    // heading += 0.0404;

    // Correct for when signs are reversed.
    if (heading < 0) {
        heading += 2 * PI;
    }

    // Check for wrap due to addition of declination.
    if (heading > 2 * PI) {
        heading -= 2 * PI;
    }

    return heading * 180 / M_PI;
}
