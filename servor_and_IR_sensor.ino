#define IR_SENSOR_PIN 0  // GPIO 0
#include <ESP32Servo.h>
#define SERVO_PIN 26
Servo myServo;
bool isOpen = false;
void setup() {
  Serial.begin(115200);
  pinMode(IR_SENSOR_PIN, INPUT);
  myServo.attach(SERVO_PIN, 500, 2500);  // Correct for ESP32 + SG90
  myServo.write(0);  // Start at 0 degrees
   pinMode(27, OUTPUT);
}

void loop() {
  int sensorValue = digitalRead(IR_SENSOR_PIN);

  if (sensorValue == LOW) {
    Serial.println("🚫 Object detected!");
    Serial.println("🔎 Object detected! Rotating servo...");
    myServo.write(95);  // Rotate to 90 degrees
    delay(2000);
    myServo.write(0);   // Return to 0 degrees
    isOpen = true;
  } else {
    Serial.println("✅ No object");
   isOpen = false;
  }

  delay(100);  // Read every 200ms
}
