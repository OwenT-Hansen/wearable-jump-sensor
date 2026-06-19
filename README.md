# wearable-jump-sensor
A wearable athletic sensor prototype using an IMU and microcontroller to calculate vertical jump displacement via airborne flight-time kinematics.

## Milestone 1: Sensor Communication Verified
- I²C scan confirmed MPU6050 responsive at address `0x68`
- SDA wired to D2 (GPIO4), SCL wired to D3 (GPIO5)
- `Wire.begin(4, 5)` required — board package does not define `D2`/`D3` by name
