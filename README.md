# Jump Tracker

Wearable vertical jump height tracker using ESP32 (Seeed XIAO ESP32C3) and MPU6050 (GY-521).

## Hardware
- Microcontroller: Seeed Studio XIAO ESP32C3
- Sensor: GY-521 (MPU6050)
- SDA: D2 (GPIO4)
- SCL: D3 (GPIO5)

## Milestone 1: Environment Verified
- USB CDC On Boot enabled to fix silent serial output
- Heartbeat firmware confirmed running on board

## Milestone 2: Sensor Communication Verified
- I2C scan confirmed MPU6050 responsive at address 0x68
- Wire.begin(4, 5) required — board package does not define D2/D3 by name