// ESP32-S3 Blue LED Pin Finder
// This sketch will blink a list of candidate GPIOs, one at a time, so you can see which controls the onboard blue LED.

#include <Arduino.h>

const int candidatePins[] = {2, 8, 18, 21, 38, 39, 40, 41, 42, 45, 46, 47, 48};
const int numPins = sizeof(candidatePins) / sizeof(candidatePins[0]);
const int blinkDelay = 500; // ms

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < numPins; ++i) {
    pinMode(candidatePins[i], OUTPUT);
    digitalWrite(candidatePins[i], LOW);
  }
  Serial.println("Starting Blue LED pin finder. Watch your board and note which pin blinks the blue LED.");
}

void loop() {
  for (int i = 0; i < numPins; ++i) {
    Serial.print("Blinking GPIO ");
    Serial.println(candidatePins[i]);
    digitalWrite(candidatePins[i], HIGH);
    delay(blinkDelay);
    digitalWrite(candidatePins[i], LOW);
    delay(blinkDelay);
  }
}
