// jump_tracker.ino
// Wearable vertical jump height tracker
// States: IDLE -> TAKEOFF -> AIRBORNE -> LANDING -> PLUGGED_IN
// Unplug to record jumps, plug in and type 's' to see results, 'c' to clear

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>
#include <math.h>
#include <MadgwickAHRS.h> 

Adafruit_MPU6050 mpu;
Preferences prefs;

// ─────────────────────────────────────────
// Calibration constants (calculated 2026-06-28)
// ─────────────────────────────────────────
const float X_OFFSET =  0.3914;
const float Y_OFFSET = -0.1574;
const float Z_OFFSET = -1.1096;
const float X_SCALE  =  0.9997;
const float Y_SCALE  =  0.9961;
const float Z_SCALE  =  0.9790;

// ─────────────────────────────────────────
// Thresholds
// TAKEOFF_THRESHOLD: spike above this confirms explosive push-off
// FREEFALL_THRESHOLD: drop below this confirms airborne
// LANDING_THRESHOLD: spike above this confirms ground contact
// TAKEOFF_WINDOW_MS: max time between takeoff spike and freefall
//                    wide enough for approach jump arm swing delay
// ─────────────────────────────────────────
const float TAKEOFF_THRESHOLD  = 15.0;
const float FREEFALL_THRESHOLD = 3;
const float LANDING_THRESHOLD  = 12.0;
const float TAKEOFF_WINDOW     = 500.0;

// ─────────────────────────────────────────
// State machine
// ─────────────────────────────────────────
enum State { IDLE, TAKEOFF, AIRBORNE, LANDING, PLUGGED_IN };
State currentState = IDLE;

// ─────────────────────────────────────────
// Jump tracking
// ─────────────────────────────────────────
unsigned long airborneStart  = 0;
unsigned long takeoffStart   = 0;
float flightTimeSeconds      = 0;
float jumpHeightMeters       = 0;
int   jumpCount              = 0;

// ─────────────────────────────────────────
// Physics: h = 0.5 * g * (t/2)²
// t/2 because flight is symmetric —
// time to peak equals time falling back down
// ─────────────────────────────────────────
float calcHeight(float t) {
  float halfT = t / 2.0;
  return 0.5 * 9.81 * halfT * halfT;
}

// ─────────────────────────────────────────
// Flash storage
// ─────────────────────────────────────────
void saveJump(float height, float flightTime) {
  prefs.begin("jumps", false);
  int stored = prefs.getInt("count", 0);
  String key = "j" + String(stored);
  String val = String(height, 4) + "," + String(flightTime, 4);
  prefs.putString(key.c_str(), val.c_str());
  prefs.putInt("count", stored + 1);
  prefs.end();
}

void dumpSummary() {
  prefs.begin("jumps", true);
  int count = prefs.getInt("count", 0);

  Serial.println("=== Jump Summary ===");
  Serial.print("Total jumps: ");
  Serial.println(count);
  Serial.println();

  if (count == 0) {
    Serial.println("No jumps recorded.");
    prefs.end();
    return;
  }

  float best  = 0;
  float total = 0;

  for (int i = 0; i < count; i++) {
    String key = "j" + String(i);
    String val = prefs.getString(key.c_str(), "");
    int comma  = val.indexOf(',');
    float h    = val.substring(0, comma).toFloat();
    float t    = val.substring(comma + 1).toFloat();

    Serial.print("  Jump ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(h * 100.0, 1);
    Serial.print(" cm | flight time: ");
    Serial.print(t, 3);
    Serial.println(" s");

    total += h;
    if (h > best) best = h;
  }

  Serial.println();
  Serial.print("  Best:    ");
  Serial.print(best * 100.0, 1);
  Serial.println(" cm");
  Serial.print("  Average: ");
  Serial.print((total / count) * 100.0, 1);
  Serial.println(" cm");
  Serial.println();
  Serial.println("=== End Summary ===");
  Serial.println();
  Serial.println("Commands: 's' = summary | 'c' = clear log");
  prefs.end();
}

void clearLog() {
  prefs.begin("jumps", false);
  prefs.clear();
  prefs.end();
  jumpCount = 0;
  Serial.println("Log cleared. Unplug to begin new session.");
}

// ─────────────────────────────────────────
// Setup
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(4, 5);  // SDA=GPIO4 (D2), SCL=GPIO5 (D3)

  if (!mpu.begin()) {
    while (1) delay(10);  // fatal — freeze
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  
  delay(100);
}

// ─────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────
void loop() {

  // ── Serial input handler ─────────────────
  if (Serial.available() > 0) {
    char cmd = Serial.read();

    if (currentState != PLUGGED_IN) {
      currentState = PLUGGED_IN;
      delay(1000);
      Serial.println("\n=== USB Connected ===");
      Serial.println("Commands: 's' = summary | 'c' = clear log");
      Serial.println();
    }

    if (cmd == 's') {
      dumpSummary();
    } else if (cmd == 'c') {
      clearLog();
    }
  }

  // ── PLUGGED_IN state ─────────────────────
  if (currentState == PLUGGED_IN) {
    if (!Serial) {
      currentState = IDLE;
    } else {
      delay(100);
    }
    return;
  }

  // ── Read and calibrate sensor ─────────────
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float aX = (accel.acceleration.x - X_OFFSET) * X_SCALE;
  float aY = (accel.acceleration.y - Y_OFFSET) * Y_SCALE;
  float aZ = (accel.acceleration.z - Z_OFFSET) * Z_SCALE;
  float totalAccel = sqrt(aX*aX + aY*aY + aZ*aZ);
  
  

  // ── State machine ─────────────────────────
  switch (currentState) {

    case IDLE:
      // waiting for explosive push-off spike
      if (totalAccel > TAKEOFF_THRESHOLD) {
        currentState = TAKEOFF;
        takeoffStart = millis();
      }
      break;

    case TAKEOFF:
      // spike detected — now wait for freefall to confirm real jump
      // if freefall doesnt follow within the window, it was a false spike
      if (totalAccel < FREEFALL_THRESHOLD) {
        currentState  = AIRBORNE;
        airborneStart = millis();
      } else if ((millis() - takeoffStart) > TAKEOFF_WINDOW) {
        // no freefall within 500ms — false spike, reset to IDLE
        currentState = IDLE;
      }
      break;

    case AIRBORNE:
      // ignore landing for first 200ms
      // filters noise during transition from takeoff to freefall
      if ((millis() - airborneStart) < 200) break;

      // timeout — stuck in freefall, reset
      if ((millis() - airborneStart) > 2000) {
        currentState = IDLE;
        break;
      }

      // timeout — stuck in freefall, reset
      if ((millis() - airborneStart) > 1000) {
        currentState = IDLE;
        break;
      }

      if (totalAccel > LANDING_THRESHOLD) {
        currentState      = LANDING;
        flightTimeSeconds = (millis() - airborneStart) / 1000.0;
        jumpHeightMeters  = calcHeight(flightTimeSeconds);
        saveJump(jumpHeightMeters, flightTimeSeconds);
        jumpCount++;
      }
      break;

    case LANDING:
      delay(500);  // let impact vibration settle
      currentState = IDLE;
      break;

    case PLUGGED_IN:
      break;
  }

  delay(5);  // 200Hz
}