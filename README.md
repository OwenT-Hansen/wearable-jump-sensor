# Wearable Jump Sensor

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

## Milestone 3: Basic Sensor Readings (In Progress)
- Implemented total acceleration magnitude (|a| = sqrt(aX²+aY²+aZ²)) for tilt-invariant jump detection
- Flat baseline: |a| ≈ 8.8 m/s² (expected 9.81 — known sensor bias)
- Tilt testing revealed |a| varies 8.8–11.1 across orientations — indicates per-axis
  calibration error (offset + scale), not a simple uniform bias
- Next: 6-point calibration routine to determine per-axis correction factors

## Milestone 4: 6-Axis Accelerometer Calibration

Raw MPU6050 readings contain two types of hardware error: **offset** (zero-point bias per 
axis) and **scale** (sensitivity error per axis). A single global correction can't fix both 
simultaneously across all three axes, so a 6-point calibration was performed — resting the 
sensor on each of its 6 faces and averaging 200 readings per face to isolate each axis.

For each axis, offset and scale are derived from the opposing +G and -G face pair:
```
offset = (positive_face + negative_face) / 2
scale  = 9.81 / ((positive_face - negative_face) / 2)
```

Each raw reading is then corrected before any further computation:
```
corrected = (raw - offset) * scale
```

After calibration, |a| = sqrt(aX² + aY² + aZ²) reads consistently at ~9.81 m/s² across 
all orientations, confirming tilt-invariance. 

## Milestone 5: Basic Jump Detection Working

State machine (IDLE -> TAKEOFF -> AIRBORNE -> LANDING) successfully detecting jumps
and storing height/flight time to onboard flash. Data retrieved via Serial Monitor
on USB connect.

Known issues:
- Occasional false triggers under certain movement conditions
- Height consistently underreported vs video ground truth (~200ms timing gap)

Next steps:
- Eliminate false triggers through threshold tuning and state machine refinement
- Investigate timing gap root cause and implement correction
- Per-user calibration routine