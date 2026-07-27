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







## Project Pivot — Simplified Scope (July 2026)

### New Objective
Track two specific athletic jumps for performance measurement:
- **Squat Jump (SJ):** No countermovement, hands at sides
- **Countermovement Jump (CMJ):** Squat then explode upward, hands at sides

Constraining to these two jump types eliminates signal processing complexity
from arm swing and approach steps, making detection and measurement more
reliable and the athletic data more meaningful.

### CMJ/SJ Ratio
The ratio of CMJ height to SJ height is a standard metric in sports science
for measuring reactive strength and elastic energy utilization:
- Ratio > 1.1: good use of stretch-shortening cycle
- Ratio < 1.0: may indicate fatigue or poor neuromuscular coordination

### Interface
Bluetooth LE serial connection to Android phone via Serial Bluetooth Terminal app.
Commands sent from phone, results printed back wirelessly. No USB required during use.

### Deprecated
Flash storage and USB Serial data retrieval removed in favor of live BLE output.
Time series plotter buffer removed — may be reintroduced for diagnostics later.


## Phase 2: WiFi Web Interface & Jump Detection (July 2026)

### Scope Change
Narrowed project focus to two standardized athletic jumps with hands at sides:
- **Squat Jump (SJ):** No countermovement, static start position
- **Countermovement Jump (CMJ):** Downward dip then explosive upward drive

This constraint eliminates arm swing and approach step interference, making
signal processing more reliable and the athletic output more meaningful.

### Interface: WiFi Access Point
Abandoned BLE after extensive troubleshooting — the XIAO ESP32C3 BLE stack
had consistent GATT connection failures across multiple libraries (NimBLE 1.4.x,
NimBLE 2.5, standard ESP32 BLE) and two different phone apps. Root cause was
never fully isolated but likely a combination of board package configuration
and Android BLE stack compatibility.

Switched to WiFi Access Point mode:
- Chip broadcasts its own hotspot ("JumpTracker", password: jump1234)
- User connects phone to hotspot and opens 192.168.4.1 in browser
- No app required — fully browser-based interface
- Works completely offline, no router needed

### Signal Processing Challenges

**Freefall detection at waist mount:**
The MPU6050 at a waist mount does not snap instantly to 0 m/s² during
freefall. The signal unloads gradually from ~9.8 down through the threshold
over many samples. This caused systematic underreporting because the timer
started late in the freefall descent.

**Solution:** Raised freefall threshold from 3.0 to 9.0 m/s² — the timer now
starts the moment gravity begins unloading rather than waiting for deep freefall.

**CMJ squat dip false trigger:**
The countermovement (squat down) phase briefly pushes |a| below the freefall
threshold, causing the state machine to enter AIRBORNE during the squat rather
than the actual jump.

**Solution:** Added TAKEOFF state — requires a push-off spike above 18.0 m/s²
before freefall can be accepted. This reflects real CMJ biomechanics: the
explosive leg drive always precedes liftoff.

**WiFi interference with timing:**
`server.handleClient()` can block for 10-50ms during packet handling, disrupting
the flight timer during an active jump.

**Solution:** WiFi handling is blocked during TAKEOFF and AIRBORNE states —
only runs during IDLE, ARMED, and LANDING.

**Mount vibration:**
Loose sensor mount caused false spikes mid-air. A secure, tight mount
significantly cleaned up the signal.

### State Machine
```
IDLE → ARMED → TAKEOFF → AIRBORNE → LANDING → IDLE
```
- **ARMED:** waiting for push-off spike above 18.0 m/s²
- **TAKEOFF:** spike detected, waiting for signal to drop below 9.0 m/s²
- **AIRBORNE:** timing flight, waiting for landing spike above 13.0 m/s²
- **LANDING:** 1.5 second settle delay, then return to IDLE

### Web Interface Features
- CMJ and SJ buttons to arm the device
- Full jump history per type with best and average
- CMJ/SJ ratio with athletic interpretation
- Diagnostic bar graph at /diag showing |a| signal colored by state
- Reset button to clear session data

### CMJ/SJ Ratio Benchmarks
| Ratio | Interpretation |
|---|---|
| ≥ 1.15 | Excellent elastic energy utilization |
| ≥ 1.08 | Good elastic energy utilization |
| ≥ 1.00 | Average stretch-shortening benefit |
| < 1.00 | Below average — CMJ not improving on SJ |

### Known Limitations
- Height consistently underreports vs video ground truth by ~15-20%
- Timing offset likely due to gradual signal unloading at waist mount
- Per-user calibration routine planned for future phase
- Session data lost on power cycle —
