// i2c_scanner.ino
// Purpose: Verify MPU6050 is reachable at 0x68

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);  // Wait for USB CDC

  Wire.begin(4, 5);  // SDA=D4, SCL=D5 by default on XIAO ESP32C3

  Serial.println("\n--- I2C Scanner ---");
  Serial.println("Scanning...");

  uint8_t deviceCount = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      Serial.println(" !");
      deviceCount++;
    }
  }

  if (deviceCount == 0)
    Serial.println("No I2C devices found. Check wiring.");
  else
    Serial.println("Scan complete.");
}

void loop() {}