# Wearable Jump Tracker

A wearable vertical jump height tracker built around the Seeed XIAO ESP32C3 and MPU6050 accelerometer. Detects squat jumps (SJ) and countermovement jumps (CMJ), calculates jump height from flight time, and serves results to a phone browser over WiFi.

---

## Hardware

| Component | Detail |
|---|---|
| Microcontroller | Seeed Studio XIAO ESP32C3 |
| Sensor | GY-521 (MPU6050) |
| SDA | D2 (GPIO4) |
| SCL | D3 (GPIO5) |
| Power | Hardwired LiPo battery |
| Mount | Waist clip |

---

## How to Use

1. Power the device
2. Connect your phone to the **JumpTracker** WiFi network (password: `jump1234`)
3. Open `192.168.4.1` in your browser
4. Tap **CMJ** or **SJ** to arm the device
5. Jump — results appear automatically within 2 seconds of landing
6. Tap **View Diagnostic Graph** to inspect the raw acceleration signal if needed

---

## Development Log

### Environment Setup

Getting the board talking to the IDE took some work. The main issues were a missing upload port and silent serial output — both fixed by enabling **USB CDC On Boot** in the Arduino IDE tools menu. Once that was set, the heartbeat firmware flashed and printed cleanly over serial.

I²C communication was verified with a scanner sketch. The MPU6050 showed up at address `0x68` as expected. One catch: the board package doesn't define `D2`/`D3` by name, so `Wire.begin(4, 5)` is required instead.

---

### Accelerometer Calibration

Raw MPU6050 readings have two types of hardware error — **offset** (zero-point bias) and **scale** (sensitivity error) — and each axis has its own independent error. A single global correction can't fix all three axes at once, so I ran a 6-point calibration: rested the sensor flat on each of its 6 faces and averaged 200 readings per face.

For each axis, offset and scale are derived from the opposing face pair:

```
offset = (positive_face + negative_face) / 2
scale  = 9.81 / ((positive_face - negative_face) / 2)
```

Each raw reading is corrected before any further computation:

```
corrected = (raw - offset) * scale
```

Correction factors applied:

| Axis | Offset | Scale |
|---|---|---|
| X | 0.3914 | 0.9997 |
| Y | -0.1574 | 0.9961 |
| Z | -1.1096 | 0.9790 |

After calibration, `|a| = sqrt(aX² + aY² + aZ²)` reads consistently at ~9.81 m/s² across all sensor orientations, which is what you'd expect from a tilt-invariant magnitude calculation.

---

### Jump Detection — Phase 1

The first detection attempt used a simple two-state machine: if `|a|` dropped below a freefall threshold, start the timer; if it spiked above a landing threshold, stop it. This worked sometimes but had two persistent problems:

- **Underreporting** — the freefall threshold was too low (3.0 m/s²), so the timer started well after the feet had already left the ground
- **False triggers** — sitting down fast, crouching, or sudden movements caused spurious detections

A lot of time went into threshold tuning, adding confirmation counters, and experimenting with minimum flight time filters. The results were inconsistent. The real issue turned out to be architectural — a two-state machine isn't robust enough for this sensor at a waist mount.

---

### Project Scope Change

Originally the goal was to track in-game volleyball jumps including approach jumps with arm swings. After working through the signal processing challenges, I narrowed the scope to two standardized jumps with hands at sides:

- **Squat Jump (SJ):** static start, no countermovement
- **Countermovement Jump (CMJ):** downward dip then explosive upward drive

This eliminates the arm swing and approach step signals that make detection much harder, and it produces more athletically meaningful data since the CMJ/SJ ratio is a standard sports science metric.

---

### Interface — BLE Attempt

The original plan was a Bluetooth LE serial connection to an Android phone. After extensive troubleshooting across NimBLE 1.4.x, NimBLE 2.5, and the standard ESP32 BLE library, every connection attempt failed with a GATT timeout error. The chip would advertise correctly but couldn't complete the handshake with either of two different BLE apps. Root cause was never fully isolated — likely a combination of the XIAO ESP32C3's specific BLE radio configuration and Android BLE stack compatibility.

Switched to WiFi Access Point mode instead. The chip creates its own hotspot and serves a web interface — no app needed, no pairing, works completely offline.

---

### Jump Detection — Phase 2

With the scope narrowed and WiFi working, I rebuilt the detection logic around a proper 5-state machine:

```
IDLE → ARMED → TAKEOFF → AIRBORNE → LANDING → IDLE
```

**The key insight was adding the TAKEOFF state.** Rather than triggering directly on freefall, the machine first requires a push-off spike above 18.0 m/s² before it will accept a freefall event as legitimate. This reflects real jump biomechanics — an explosive leg drive always precedes liftoff — and it filters out squat dips, sitting down, and other movements that dip below the freefall threshold without a preceding spike.

The freefall threshold was also raised significantly from 3.0 to 9.0 m/s². At a waist mount the signal doesn't snap to zero instantly — it unloads gradually from ~9.8 downward over many samples. Starting the timer at 9.0 captures much more of the true flight window than waiting for deep freefall.

WiFi packet handling (`server.handleClient()`) is blocked during TAKEOFF and AIRBORNE states. Even a 10-20ms WiFi interrupt is enough to corrupt the flight timer.

**State thresholds:**

| State transition | Threshold |
|---|---|
| ARMED → TAKEOFF | `\|a\|` > 18.0 m/s² |
| TAKEOFF → AIRBORNE | `\|a\|` < 9.0 m/s² (CMJ) or 8.0 m/s² (SJ) |
| AIRBORNE → LANDING | `\|a\|` > 10.0 m/s² |
| LANDING → IDLE | 1.5 second settle delay |

---

### Calibration

Flight time converts to height using the flight time symmetry formula:

```
h = 0.5 × g × (t/2)²
```

where `t/2` is used because the jump is symmetric — time going up equals time coming down.

I validated accuracy by jumping to a fixed known height (29cm to ceiling) and comparing device readings to ground truth. Twenty jumps across both CMJ and SJ gave an average overreport of 7.1cm, consistent across both jump types. This offset is subtracted from every reading:

```
height = max(0, raw_height - 0.071)
```

Jump-to-jump variability was approximately ±2-3cm, which is within acceptable range for a waist-mounted accelerometer. The primary source of variability is landing style — knee bend after ground contact extends the deceleration phase and can inflate the reading.

---

### CMJ/SJ Ratio

The ratio of CMJ to SJ height is a standard metric in sports science for measuring how well an athlete uses the stretch-shortening cycle:

| Ratio | Interpretation |
|---|---|
| ≥ 1.15 | Excellent elastic energy utilization |
| ≥ 1.08 | Good elastic energy utilization |
| ≥ 1.00 | Average stretch-shortening benefit |
| < 1.00 | CMJ not improving on SJ — may indicate fatigue |

---

### Known Limitations

- Session data is lost on power cycle — flash storage was prototyped but removed to simplify the codebase
- Landing style affects accuracy — consistent natural landings give more repeatable results than forced stiff landings
- The 7.1cm calibration offset is specific to this sensor unit and mount position; different users or mount locations may need recalibration
- BLE was never resolved — WiFi is the only wireless interface

---

## Libraries

- Adafruit MPU6050
- Adafruit Unified Sensor
- Adafruit BusIO 