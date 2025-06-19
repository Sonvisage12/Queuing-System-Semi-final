#include <Wire.h>
#include "RTClib.h"

RTC_DS3231 rtc;

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for Serial Monitor
  
  if (!rtc.begin()) {
    Serial.println("❌ Could not find RTC!");
    while (1);
  }

  // Check if RTC lost power (needs time reset)
  if (rtc.lostPower()) {
    Serial.println("⚠ RTC lost power! Setting time to compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Uncomment to manually set time (YYYY, MM, DD, HH, MM, SS)
  // rtc.adjust(DateTime(2025, 4, 24, 12, 0, 0));
}

void loop() {
  DateTime now = rtc.now();

  // Print Date (YYYY/MM/DD)
  Serial.print("📅 Date: ");
  Serial.print(now.year());
  Serial.print("/");
  Serial.print(now.month());
  Serial.print("/");
  Serial.println(now.day());

  // Print Time (HH:MM:SS)
  Serial.print("⏰ Time: ");
  Serial.print(now.hour());
  Serial.print(":");
  Serial.print(now.minute());
  Serial.print(":");
  Serial.println(now.second());

  // Print Temperature (DS3231 has a built-in sensor)
  Serial.print("🌡️ Temp: ");
  Serial.print(rtc.getTemperature());
  Serial.println("°C");

  Serial.println("------------------");
  delay(2000); // Update every 2 seconds
}
