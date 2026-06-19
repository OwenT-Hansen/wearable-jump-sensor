// basic_readings.ino
// Now computing tilt-invariant total acceleration magnitude

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>

Adafruit_MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Wire.begin(4, 5);  // SDA=GPIO4 (D2), SCL=GPIO5 (D3)

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check wiring.");
    while (1) delay(10);
  }

  Serial.println("MPU6050 ready.");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  delay(100);
}

void loop() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float aX = accel.acceleration.x;
  float aY = accel.acceleration.y;
  float aZ = accel.acceleration.z;

  float totalAccel = sqrt(aX*aX + aY*aY + aZ*aZ);

  Serial.print("aX:"); Serial.print(aX, 3);
  Serial.print(" aY:"); Serial.print(aY, 3);
  Serial.print(" aZ:"); Serial.print(aZ, 3);
  Serial.print(" | |a|:"); Serial.println(totalAccel, 3);

  delay(50);  // ~20Hz sample rate — fine for now, may increase later
}