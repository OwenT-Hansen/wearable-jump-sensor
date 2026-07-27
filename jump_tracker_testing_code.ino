// jump_tracker.ino
// wearable vertical jump height tracker for squat jumps and countermovement jumps
// connects to phone via wifi - open 192.168.4.1 in browser after connecting to JumpTracker network

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// wifi access point - chip creates its own hotspot, no router needed
const char* AP_SSID = "JumpTracker";
const char* AP_PASS = "jump1234";

WebServer server(80);

// per-axis offset and scale corrections calculated from 6-point calibration on 2026-06-28
// each axis has its own bias so a single global correction wasnt enough
const float X_OFFSET =  0.3914;
const float Y_OFFSET = -0.1574;
const float Z_OFFSET = -1.1096;
const float X_SCALE  =  0.9997;
const float Y_SCALE  =  0.9961;
const float Z_SCALE  =  0.9790;

// detection thresholds - tuned through testing
// takeoff: push-off leg drive spike before leaving the ground
// freefall: signal drops below gravity as feet leave ground
//           cmj uses a higher threshold because the countermovement unloads faster
// landing: impact spike on ground contact
// timeouts: prevent getting stuck in a state if something goes wrong
const float TAKEOFF_THRESHOLD      = 18.0;
const float FREEFALL_THRESHOLD_CMJ =  9.0;
const float FREEFALL_THRESHOLD_SJ  =  8.0;
const float LANDING_THRESHOLD      = 10.0;
const int   MIN_FLIGHT_MS_CMJ      =  300;
const int   MIN_FLIGHT_MS_SJ       =  200;
const int   MAX_FLIGHT_MS          = 2000;
const int   TAKEOFF_TIMEOUT_MS     =  600;

// jump types
enum JumpType { NONE, CMJ, SJ };
JumpType currentJumpType = NONE;

// state machine - order matters, chip moves through these in sequence for each jump
// idle -> armed -> takeoff -> airborne -> landing -> back to idle
enum State { IDLE, ARMED, TAKEOFF, AIRBORNE, LANDING };
State currentState = IDLE;

// timing variables - micros() used for airborne to get sub-millisecond precision
unsigned long airborneStart = 0;
unsigned long takeoffStart  = 0;

// stores up to 20 jumps per type per session, resets on power cycle
const int MAX_JUMPS = 20;
float cmjResults[MAX_JUMPS];
float sjResults[MAX_JUMPS];
int   cmjCount = 0;
int   sjCount  = 0;
float lastCMJ  = 0;
float lastSJ   = 0;
String lastResult   = "No jump recorded yet.";
String deviceStatus = "Idle — select a jump type.";

// diagnostic buffer - records raw acceleration during each jump
// viewable at 192.168.4.1/diag as a color coded bar chart
const int DIAG_SAMPLES = 500;
float diagBuffer[DIAG_SAMPLES];
int   diagIndex = 0;

Adafruit_MPU6050 mpu;

// standard flight time to height conversion
// uses half the total flight time because the jump is symmetric -
// time going up equals time coming down
// 7.1cm offset applied based on 20-jump empirical calibration against known height
float calcHeight(float t) {
  float halfT = t / 2.0;
  float raw   = 0.5 * 9.81 * halfT * halfT;
  return max(0.0f, raw - 0.071f);
}

// cmj/sj ratio benchmarks from sports science literature
String interpretRatio(float ratio) {
  if (ratio >= 1.15) return "Excellent elastic energy utilization";
  if (ratio >= 1.08) return "Good elastic energy utilization";
  if (ratio >= 1.00) return "Average stretch-shortening benefit";
  return "Below average — CMJ not improving on SJ";
}

// builds the results cards for both jump types including best, average, and full history
String buildResultsSection() {
  String out = "";

  if (cmjCount > 0) {
    float total = 0;
    float best  = 0;
    for (int i = 0; i < cmjCount; i++) {
      total += cmjResults[i];
      if (cmjResults[i] > best) best = cmjResults[i];
    }
    float avg = total / cmjCount;
    out += "<div class='card'>";
    out += "<h2>CMJ Results</h2>";
    out += "<p>Best: <b>" + String(best * 100.0, 1) + " cm</b></p>";
    out += "<p>Average: <b>" + String(avg * 100.0, 1) + " cm</b></p>";
    out += "<p>Jumps:</p><ul>";
    for (int i = 0; i < cmjCount; i++) {
      out += "<li>Jump " + String(i + 1) + ": " +
             String(cmjResults[i] * 100.0, 1) + " cm</li>";
    }
    out += "</ul></div>";
  }

  if (sjCount > 0) {
    float total = 0;
    float best  = 0;
    for (int i = 0; i < sjCount; i++) {
      total += sjResults[i];
      if (sjResults[i] > best) best = sjResults[i];
    }
    float avg = total / sjCount;
    out += "<div class='card'>";
    out += "<h2>SJ Results</h2>";
    out += "<p>Best: <b>" + String(best * 100.0, 1) + " cm</b></p>";
    out += "<p>Average: <b>" + String(avg * 100.0, 1) + " cm</b></p>";
    out += "<p>Jumps:</p><ul>";
    for (int i = 0; i < sjCount; i++) {
      out += "<li>Jump " + String(i + 1) + ": " +
             String(sjResults[i] * 100.0, 1) + " cm</li>";
    }
    out += "</ul></div>";
  }

  if (cmjCount == 0 && sjCount == 0) {
    out += "<div class='card'><h2>Results</h2>"
           "<p>No jumps recorded yet.</p></div>";
  }

  // ratio only shows once both jump types have been recorded
  if (lastCMJ > 0 && lastSJ > 0) {
    float ratio = lastCMJ / lastSJ;
    out += "<div class='card'>"
           "<h2>CMJ / SJ Ratio</h2>"
           "<p>Last CMJ: <b>" + String(lastCMJ * 100.0, 1) + " cm</b></p>"
           "<p>Last SJ: <b>"  + String(lastSJ  * 100.0, 1) + " cm</b></p>"
           "<p>Ratio: <b>" + String(ratio, 2) + "</b></p>"
           "<p>" + interpretRatio(ratio) + "</p>"
           "</div>";
  }

  return out;
}

// main page - auto refreshes every 2 seconds so results appear without manual reload
String buildPage() {
  String html = R"(<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <meta http-equiv='refresh' content='2'>
  <title>Jump Tracker</title>
  <style>
    body  { font-family:Arial,sans-serif; max-width:400px; margin:0 auto; padding:20px; background:#1a1a2e; color:#eee; }
    h1    { color:#00d4ff; text-align:center; }
    h2    { color:#00d4ff; margin-top:0; }
    .status { background:#16213e; padding:15px; border-radius:8px; margin:15px 0; text-align:center; font-size:1.1em; }
    .btn  { display:block; width:100%; padding:18px; margin:10px 0; font-size:1.2em; font-weight:bold; border:none; border-radius:8px; cursor:pointer; box-sizing:border-box; }
    .cmj  { background:#00d4ff; color:#1a1a2e; }
    .sj   { background:#00ff88; color:#1a1a2e; }
    .reset{ background:#ff4444; color:white; }
    .diag { background:#555; color:white; font-size:0.9em; padding:12px; }
    .card { background:#16213e; padding:15px; border-radius:8px; margin:15px 0; }
    p     { margin:8px 0; }
    ul    { margin:5px 0; padding-left:20px; }
    li    { margin:4px 0; font-size:0.95em; }
  </style>
</head>
<body>
  <h1>Jump Tracker</h1>
  <div class='status'>)" + deviceStatus + R"(</div>
  <form action='/cmj' method='get'>
    <button class='btn cmj' type='submit'>Countermovement Jump (CMJ)</button>
  </form>
  <form action='/sj' method='get'>
    <button class='btn sj' type='submit'>Squat Jump (SJ)</button>
  </form>
  )" + buildResultsSection() + R"(
  <form action='/reset' method='get'>
    <button class='btn reset' type='submit'>Reset Results</button>
  </form>
  <form action='/diag' method='get'>
    <button class='btn diag' type='submit'>View Diagnostic Graph</button>
  </form>
</body>
</html>)";

  return html;
}

// diagnostic page - color coded bar chart of the raw acceleration signal
// green = below freefall threshold, red = above landing threshold, blue = normal
// useful for verifying the state machine is detecting jumps correctly
void handleDiag() {
  String bars = "";
  int count = min(diagIndex, DIAG_SAMPLES);

  for (int i = 0; i < count; i++) {
    float val = diagBuffer[i];
    int pct = (int)((val / 30.0) * 100.0);
    if (pct > 100) pct = 100;

    String color = "#00d4ff";
    if (val < FREEFALL_THRESHOLD_CMJ) color = "#00ff88";
    if (val > LANDING_THRESHOLD)      color = "#ff4444";

    bars += "<div style='display:flex;align-items:center;margin:1px 0;'>"
            "<span style='width:38px;font-size:10px;color:#aaa;'>"
            + String(val, 1) + "</span>"
            "<div style='height:12px;width:" + String(pct) +
            "%;background:" + color + ";'></div></div>";
  }

  String html = R"(<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Jump Diagnostic</title>
  <style>
    body { background:#1a1a2e; color:#eee; font-family:Arial,sans-serif; padding:10px; }
    h2   { color:#00d4ff; }
    .legend { margin:10px 0; font-size:12px; }
    .legend span { margin-right:15px; }
  </style>
</head>
<body>
  <h2>Jump Diagnostic</h2>
  <div class='legend'>
    <span style='color:#00ff88'>&#9632; Freefall</span>
    <span style='color:#ff4444'>&#9632; Landing (&gt;)" + String(LANDING_THRESHOLD, 1) + R"()</span>
    <span style='color:#00d4ff'>&#9632; Normal</span>
  </div>
  <p style='font-size:12px;color:#aaa'>)" + String(count) + " samples | " +
    String(count * 5) + R"(ms total</p>
  )" + bars + R"(
  <br>
  <a href='/' style='color:#00d4ff'>&larr; Back</a>
</body>
</html>)";

  server.send(200, "text/html", html);
}

void handleRoot() {
  server.send(200, "text/html", buildPage());
}

void handleCMJ() {
  currentJumpType = CMJ;
  currentState    = ARMED;
  diagIndex       = 0;
  deviceStatus    = "CMJ armed — stand still then jump.";
  lastResult      = "Waiting for jump...";
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSJ() {
  currentJumpType = SJ;
  currentState    = ARMED;
  diagIndex       = 0;
  deviceStatus    = "SJ armed — hold squat position then jump.";
  lastResult      = "Waiting for jump...";
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset() {
  lastCMJ         = 0;
  lastSJ          = 0;
  cmjCount        = 0;
  sjCount         = 0;
  currentJumpType = NONE;
  currentState    = IDLE;
  diagIndex       = 0;
  deviceStatus    = "Reset — select a jump type.";
  lastResult      = "No jump recorded yet.";
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // wifi starts before the sensor to avoid initialization conflicts
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(500);
  Serial.print("access point started at: ");
  Serial.println(WiFi.softAPIP());

  Wire.begin(4, 5);  // sda = gpio4 (d2), scl = gpio5 (d3)
  if (!mpu.begin()) {
    Serial.println("mpu6050 not found - check wiring");
    while (1) delay(10);
  }

  // range and filter settings chosen for jump detection
  // 8g range captures the full push-off and landing spike without clipping
  // 21hz filter removes high frequency vibration noise while keeping jump signal intact
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  server.on("/",      handleRoot);
  server.on("/cmj",   handleCMJ);
  server.on("/sj",    handleSJ);
  server.on("/reset", handleReset);
  server.on("/diag",  handleDiag);
  server.begin();

  Serial.println("ready - connect phone to JumpTracker wifi then open 192.168.4.1");
}

void loop() {

  // wifi packet handling is blocked during takeoff and airborne states
  // even a short wifi interrupt can corrupt the flight timer
  if (currentState != AIRBORNE && currentState != TAKEOFF) {
    server.handleClient();
  }

  if (currentState == IDLE) {
    delay(5);
    return;
  }

  // landing settle delay - gives time for impact vibration to die down
  // before resetting to idle and accepting the next jump command
  if (currentState == LANDING) {
    delay(1500);
    currentState    = IDLE;
    currentJumpType = NONE;
    deviceStatus    = "Jump complete — select next jump type.";
    return;
  }

  // read sensor and apply per-axis calibration correction
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float aX = (accel.acceleration.x - X_OFFSET) * X_SCALE;
  float aY = (accel.acceleration.y - Y_OFFSET) * Y_SCALE;
  float aZ = (accel.acceleration.z - Z_OFFSET) * Z_SCALE;

  // total acceleration magnitude - tilt invariant so sensor angle doesnt matter
  float totalAccel = sqrt(aX*aX + aY*aY + aZ*aZ);

  if (diagIndex < DIAG_SAMPLES) {
    diagBuffer[diagIndex++] = totalAccel;
  }

  switch (currentState) {

    case ARMED:
      // waiting for the push-off spike that confirms an intentional jump
      // prevents squat dips or accidental movement from triggering detection
      if (totalAccel > TAKEOFF_THRESHOLD) {
        currentState = TAKEOFF;
        takeoffStart = millis();
        deviceStatus = "Takeoff detected...";
      }
      break;

    case TAKEOFF: {
      // waiting for the signal to drop below gravity, meaning feet left the ground
      // cmj and sj have different thresholds because their unloading profiles differ
      float freefallThresh = (currentJumpType == CMJ) ?
        FREEFALL_THRESHOLD_CMJ : FREEFALL_THRESHOLD_SJ;

      if (totalAccel < freefallThresh) {
        currentState  = AIRBORNE;
        airborneStart = micros();
        deviceStatus  = "Airborne...";
      } else if ((millis() - takeoffStart) > TAKEOFF_TIMEOUT_MS) {
        // spike didnt lead to freefall within the window - probably not a real jump
        currentState = ARMED;
        deviceStatus = (currentJumpType == CMJ) ?
          "CMJ armed — stand still then jump." :
          "SJ armed — hold squat position then jump.";
      }
      break;
    }

    case AIRBORNE: {
      unsigned long timeInAir = (micros() - airborneStart) / 1000;

      // safety timeout - if airborne for more than 2 seconds something went wrong
      if (timeInAir > MAX_FLIGHT_MS) {
        currentState = ARMED;
        deviceStatus = "Timeout — try again.";
        diagIndex    = 0;
        break;
      }

      // minimum flight time filter - rejects very short events that arent real jumps
      // cmj needs longer minimum because the countermovement can cause brief dips
      int minFlight = (currentJumpType == CMJ) ? MIN_FLIGHT_MS_CMJ : MIN_FLIGHT_MS_SJ;
      if (timeInAir < minFlight) break;

      if (totalAccel > LANDING_THRESHOLD) {
        currentState     = LANDING;
        float flightTime = timeInAir / 1000.0;
        float height     = calcHeight(flightTime);
        String label     = (currentJumpType == CMJ) ? "CMJ" : "SJ";

        if (currentJumpType == CMJ) {
          lastCMJ = height;
          if (cmjCount < MAX_JUMPS) cmjResults[cmjCount++] = height;
        }
        if (currentJumpType == SJ) {
          lastSJ = height;
          if (sjCount < MAX_JUMPS) sjResults[sjCount++] = height;
        }

        lastResult = label + ": " + String(height * 100.0, 1) +
                     " cm | flight time: " + String(flightTime, 3) + "s";

        if (lastCMJ > 0 && lastSJ > 0) {
          float ratio  = lastCMJ / lastSJ;
          deviceStatus = "Done! CMJ/SJ ratio: " + String(ratio, 2);
        } else {
          deviceStatus = label + " complete — do next jump or view diagnostic.";
        }
      }
      break;
    }

    case IDLE:
    case LANDING:
      break;
  }

  delay(5);  // ~200hz sample rate
}