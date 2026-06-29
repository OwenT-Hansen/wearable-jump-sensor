// basic_readings.ino
// Tilt-invariant total acceleration magnitude with per-axis calibration

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>

Adafruit_MPU6050 mpu;

// Per-axis calibration constants (calculated 2026-06-28)
const float X_OFFSET =  0.3914;
const float Y_OFFSET = -0.1574;
const float Z_OFFSET = -1.1096;
const float X_SCALE  =  0.9997;
const float Y_SCALE  =  0.9961;
const float Z_SCALE  =  0.9790;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Wire.begin(4, 5);  // SDA=GPIO4 (D2), SCL=GPIO5 (D3)

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check wiring.");
    while (1) delay(10);
  }

  Serial.println("MPU6050 ready. Calibration active.");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  delay(100);
}

void loop() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  // Apply per-axis offset and scale correction
  float aX = (accel.acceleration.x - X_OFFSET) * X_SCALE;
  float aY = (accel.acceleration.y - Y_OFFSET) * Y_SCALE;
  float aZ = (accel.acceleration.z - Z_OFFSET) * Z_SCALE;

  float totalAccel = sqrt(aX*aX + aY*aY + aZ*aZ);

  Serial.print("aX:"); Serial.print(aX, 3);
  Serial.print(" aY:"); Serial.print(aY, 3);
  Serial.print(" aZ:"); Serial.print(aZ, 3);
  Serial.print(" | |a|:"); Serial.println(totalAccel, 3);

  delay(50);
}