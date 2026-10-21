/*
  ======================================================================
  MEDIBOT - Time-Slotted Autonomous Medicine Delivery Robot
  ESP32 (30-pin) firmware.  Arduino core 2.x or 3.x.  NO extra libraries.
  ----------------------------------------------------------------------
  Pins (match MEDIBOT_WIRING_AND_POWERUP guide):
    Left L298N : IN1+IN3=16  IN2+IN4=17  ENA+ENB=18 (PWM)
    Right L298N: IN1+IN3=19  IN2+IN4=23  ENA+ENB=25 (PWM)
    HC-SR04 TRIG=26  ECHO=27 (through 1K/2K divider)
    Speed sensors: left=34  right=35
    I2C: SDA=21 SCL=22 (MPU6050 0x68 + LCD 0x27/0x3F)
    Buzzer=32   Confirm button=33 (to GND)
  Serial monitor: 115200 baud. Type  h  for the test menu.
  ======================================================================
*/
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <sys/time.h>

// ======================================================================
// 1. SETTINGS YOU MUST / MAY CHANGE
// ======================================================================
const char* WIFI_SSID = "YOUR_WIFI_NAME";        // 2.4 GHz network or phone hotspot
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* AP_SSID   = "Medibot";               // used only if Wi-Fi fails
const char* AP_PASS   = "medibot123";
const char* TZ_STRING = "IST-5:30";              // India time
const char* NTFY_TOPIC = "YOUR_NTFY_TOPIC"; // make it unique!

// ---- Route: list of steps. 'F' = forward N cm, 'T' = turn N degrees
//      (+ = right/clockwise, - = left). Edit to match your room layout.
struct Step { char type; float value; };
const Step ROUTE[] = {
  {'F', 150},
  {'T',  90},
  {'F', 100}
};

// ---- Tuning (see MEDIBOT_TESTING_GUIDE.md) ----
const int   DRIVE_PWM       = 170;   // forward speed 0-255 (raise if too slow)
const int   TURN_PWM        = 170;   // max turning speed
const int   TURN_MIN_PWM    = 130;   // min turning speed (motors stall below this)
const bool  LEFT_INVERT     = false; // true if the whole left side runs backwards
const bool  RIGHT_INVERT    = false; // true if the whole right side runs backwards
const float CM_PER_PULSE    = 1.0f;  // wheel circumference / disc slots. CALIBRATE!
const float GYRO_SIGN       = -1.0f; // flip to +1 if turns go the wrong way
const float KP_HEADING      = 4.0f;  // straight-line correction strength
const float TURN_TOL_DEG    = 2.0f;  // turn accuracy
const float OBSTACLE_CM     = 20.0f; // stop if closer than this
const uint32_t CONFIRM_TIMEOUT_MS = 120000UL;   // 2 min -> missed dose alert
const uint32_t GIVE_UP_MS         = 900000UL;   // 15 min after alert: log missed, go home

// ======================================================================
// 2. PINS
// ======================================================================
const int PIN_L_IN1 = 16, PIN_L_IN2 = 17, PIN_L_EN = 18;
const int PIN_R_IN1 = 19, PIN_R_IN2 = 23, PIN_R_EN = 25;
const int PIN_TRIG = 26, PIN_ECHO = 27;
const int PIN_ENC_L = 34, PIN_ENC_R = 35;
const int PIN_SDA = 21, PIN_SCL = 22;
const int PIN_BUZZER = 32, PIN_BUTTON = 33;

// ======================================================================
// 3. GLOBALS
// ======================================================================
enum State { ST_IDLE, ST_DELIVERING, ST_OBSTACLE, ST_WAITING, ST_MISSED, ST_RETURNING, ST_STOPPED };

WebServer server(80);
Preferences prefs;

struct Slot { uint8_t h; uint8_t m; bool on; };
Slot slots[4] = { {8,0,false}, {13,0,false}, {18,0,false}, {21,0,false} };
int  lastKey[4] = { -1, -1, -1, -1 };

State  st = ST_IDLE;
String statusText = "Starting";
String lcd1 = "", lcd2 = "";          // what is on the LCD (padded to 16)
String histStore = "";               // "YYYY-MM-DD HH:MM,status,slot\n" ...
String failReason = "";

volatile bool startReq = false;
volatile bool abortReq = false;
volatile bool motorTestReq = false;
int  pendingSlot = -1;
char manualCmd = 'S';
int  manualSpeed = 170;
uint32_t manualUntil = 0;
bool manualActive = false;

bool mpuOk = false, lcdOk = false;
uint8_t lcdAddr = 0;
float gyroBias = 0, heading = 0, desiredHeading = 0;
uint32_t lastGyroUs = 0;
float lastDist = 999;
int   closeCount = 0;
bool  obstacleNear = false;
uint32_t lastSonarMs = 0;

volatile uint32_t encL = 0, encR = 0, lastEncLus = 0, lastEncRus = 0;
const uint32_t ENC_DEBOUNCE_US = 800;

// forward declarations
void pump();
void setState(State s, const String& text);
void lcdShow(const String& a, const String& b);

// ======================================================================
// 4. SMALL HELPERS
// ======================================================================
bool ready() { return st == ST_IDLE || st == ST_STOPPED; }

const char* stateName(State s) {
  switch (s) {
    case ST_IDLE: return "IDLE";
    case ST_DELIVERING: return "DELIVERING";
    case ST_OBSTACLE: return "OBSTACLE";
    case ST_WAITING: return "WAITING";
    case ST_MISSED: return "MISSED";
    case ST_RETURNING: return "RETURNING";
    case ST_STOPPED: return "STOPPED";
  }
  return "IDLE";
}

void setState(State s, const String& text) {
  st = s;
  statusText = text;
  Serial.printf("[STATE] %s - %s\n", stateName(s), text.c_str());
}

void waitMs(uint32_t ms) {
  uint32_t t = millis();
  while (millis() - t < ms) { pump(); delay(2); }
}

String two(int v) { char b[8]; snprintf(b, sizeof b, "%02d", v); return String(b); }

bool timeSynced() { return time(nullptr) > 1700000000L; }
void nowTm(struct tm &t) { time_t n = time(nullptr); localtime_r(&n, &t); }

String timeStr() {
  if (!timeSynced()) return "--:--:--";
  struct tm t; nowTm(t);
  return two(t.tm_hour) + ":" + two(t.tm_min) + ":" + two(t.tm_sec);
}

String dateTimeStr() {
  if (!timeSynced()) return "unsynced";
  struct tm t; nowTm(t);
  char b[24];
  snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
  return String(b);
}

String slotStr(int i) { return two(slots[i].h) + ":" + two(slots[i].m); }

String nextDoseStr() {
  if (!timeSynced()) return "--:--";
  struct tm t; nowTm(t);
  int nowMin = t.tm_hour * 60 + t.tm_min;
  int best = 100000;
  for (int i = 0; i < 4; i++) {
    if (!slots[i].on) continue;
    int d = slots[i].h * 60 + slots[i].m - nowMin;
    if (d < 0) d += 1440;
    if (d < best) best = d;
  }
  if (best == 100000) return "--:--";
  int tgt = (nowMin + best) % 1440;
  return two(tgt / 60) + ":" + two(tgt % 60);
}

String ipStr() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}

// ======================================================================
// 5. LCD 16x2 (I2C PCF8574 backpack, own tiny driver - no library)
// ======================================================================
bool i2cPresent(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

void lcdRaw(uint8_t d) { Wire.beginTransmission(lcdAddr); Wire.write((uint8_t)(d | 0x08)); Wire.endTransmission(); }
void lcdNibble(uint8_t nib, uint8_t rs) {
  uint8_t d = (uint8_t)((nib << 4) | rs);
  lcdRaw(d | 0x04); delayMicroseconds(2);
  lcdRaw(d);        delayMicroseconds(60);
}
void lcdSend(uint8_t v, uint8_t rs) { lcdNibble(v >> 4, rs); lcdNibble(v & 0x0F, rs); }

void lcdInit() {
  if (i2cPresent(0x27)) lcdAddr = 0x27;
  else if (i2cPresent(0x3F)) lcdAddr = 0x3F;
  else { lcdOk = false; Serial.println("[LCD] not found (robot will still work)"); return; }
  lcdOk = true;
  delay(50);
  lcdNibble(0x03, 0); delay(5);
  lcdNibble(0x03, 0); delay(1);
  lcdNibble(0x03, 0);
  lcdNibble(0x02, 0);
  lcdSend(0x28, 0); lcdSend(0x0C, 0); lcdSend(0x06, 0); lcdSend(0x01, 0); delay(3);
  Serial.printf("[LCD] found at 0x%02X\n", lcdAddr);
}

String pad16(const String& s) {
  String r = s;
  if (r.length() > 16) r = r.substring(0, 16);
  while (r.length() < 16) r += ' ';
  return r;
}

void lcdWriteLine(uint8_t line, const String& s) {
  lcdSend(line == 0 ? 0x80 : 0xC0, 0);
  for (int i = 0; i < 16; i++) lcdSend((uint8_t)s[i], 1);
}

void lcdShow(const String& a, const String& b) {
  String p1 = pad16(a), p2 = pad16(b);
  bool c1 = (p1 != lcd1), c2 = (p2 != lcd2);
  if (!c1 && !c2) return;
  lcd1 = p1; lcd2 = p2;
  if (!lcdOk) return;
  if (c1) lcdWriteLine(0, p1);
  if (c2) lcdWriteLine(1, p2);
}

// ======================================================================
// 6. MOTORS
// ======================================================================
void pwmSetup() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_L_EN, 1000, 8);
  ledcAttach(PIN_R_EN, 1000, 8);
#else
  ledcSetup(0, 1000, 8); ledcAttachPin(PIN_L_EN, 0);
  ledcSetup(1, 1000, 8); ledcAttachPin(PIN_R_EN, 1);
#endif
}

void pwmWrite(int side, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(side == 0 ? PIN_L_EN : PIN_R_EN, duty);
#else
  ledcWrite(side, duty);
#endif
}

// side 0 = left, 1 = right. speed -255..255 (positive = forward)
void setSide(int side, int speed) {
  speed = constrain(speed, -255, 255);
  bool inv = (side == 0) ? LEFT_INVERT : RIGHT_INVERT;
  if (inv) speed = -speed;
  int a = (side == 0) ? PIN_L_IN1 : PIN_R_IN1;
  int b = (side == 0) ? PIN_L_IN2 : PIN_R_IN2;
  if (speed > 0)      { digitalWrite(a, HIGH); digitalWrite(b, LOW); }
  else if (speed < 0) { digitalWrite(a, LOW);  digitalWrite(b, HIGH); }
  else                { digitalWrite(a, LOW);  digitalWrite(b, LOW); }
  pwmWrite(side, abs(speed));
}

void drive(int l, int r) { setSide(0, l); setSide(1, r); }
void stopMotors() { drive(0, 0); }

void brake() {   // short active brake, then release
  digitalWrite(PIN_L_IN1, HIGH); digitalWrite(PIN_L_IN2, HIGH);
  digitalWrite(PIN_R_IN1, HIGH); digitalWrite(PIN_R_IN2, HIGH);
  pwmWrite(0, 255); pwmWrite(1, 255);
  waitMs(80);
  stopMotors();
}

// ======================================================================
// 7. SPEED SENSORS (interrupts)
// ======================================================================
void IRAM_ATTR isrL() { uint32_t t = micros(); if (t - lastEncLus > ENC_DEBOUNCE_US) { encL++; lastEncLus = t; } }
void IRAM_ATTR isrR() { uint32_t t = micros(); if (t - lastEncRus > ENC_DEBOUNCE_US) { encR++; lastEncRus = t; } }

void resetEnc() { noInterrupts(); encL = 0; encR = 0; interrupts(); }

float travelledCm() {
  noInterrupts(); uint32_t l = encL, r = encR; interrupts();
  uint32_t c = (l && r) ? (l + r) / 2 : (l > r ? l : r);
  return c * CM_PER_PULSE;
}

// ======================================================================
// 8. MPU6050 GYRO (raw I2C, Z axis only)
// ======================================================================
const uint8_t MPU_ADDR = 0x68;

bool mpuInit() {
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1A); Wire.write(0x03); Wire.endTransmission(); // DLPF ~44Hz
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1B); Wire.write(0x08); Wire.endTransmission(); // +-500 dps
  return true;
}

bool readGyroZRaw(int16_t &v) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x47);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2) != 2) return false;
  uint8_t hi = Wire.read(), lo = Wire.read();
  v = (int16_t)((hi << 8) | lo);
  return true;
}

void updateGyro() {
  if (!mpuOk) return;
  int16_t raw;
  if (!readGyroZRaw(raw)) return;
  uint32_t now = micros();
  float dt = (now - lastGyroUs) * 1e-6f;
  lastGyroUs = now;
  if (dt > 0.5f || dt < 0) return;
  float dps = raw / 65.5f - gyroBias;
  if (fabsf(dps) < 0.15f) dps = 0;
  heading += dps * dt * GYRO_SIGN;
}

void calibrateGyro(int samples = 400) {
  if (!mpuOk) return;
  double sum = 0; int n = 0; int16_t raw;
  for (int i = 0; i < samples; i++) {
    if (readGyroZRaw(raw)) { sum += raw; n++; }
    delay(3);
  }
  if (n > 0) gyroBias = (float)(sum / n) / 65.5f;
  heading = 0; desiredHeading = 0;
  lastGyroUs = micros();
  Serial.printf("[GYRO] bias = %.3f dps (%d samples)\n", gyroBias, n);
}

// ======================================================================
// 9. ULTRASONIC HC-SR04
// ======================================================================
float readSonarCm() {
  digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long d = pulseIn(PIN_ECHO, HIGH, 12000UL);   // ~2 m max
  if (d == 0) return 999;
  return d * 0.0343f / 2.0f;
}

void updateSonar() {
  if (millis() - lastSonarMs < 60) return;
  lastSonarMs = millis();
  float d = readSonarCm();
  lastDist = d;
  if (d < OBSTACLE_CM) closeCount++;
  else if (d >= OBSTACLE_CM + 3) closeCount = 0;
  obstacleNear = (closeCount >= 2);       // two readings in a row
}

// ======================================================================
// 10. BUZZER + BUTTON
// ======================================================================
void beepOnce(int ms) { digitalWrite(PIN_BUZZER, HIGH); delay(ms); digitalWrite(PIN_BUZZER, LOW); }
void beepTimes(int n) { for (int i = 0; i < n; i++) { beepOnce(120); delay(120); } }

bool buttonPressed() {
  if (digitalRead(PIN_BUTTON) == LOW) {
    delay(30);
    if (digitalRead(PIN_BUTTON) == LOW) {
      uint32_t t = millis();
      while (digitalRead(PIN_BUTTON) == LOW && millis() - t < 1000) delay(5);
      return true;
    }
  }
  return false;
}

// ======================================================================
// 11. STORAGE: schedule + history
// ======================================================================
void loadAll() {
  prefs.begin("medibot", false);
  for (int i = 0; i < 4; i++) {
    slots[i].h  = prefs.getUChar(("h" + String(i)).c_str(), slots[i].h);
    slots[i].m  = prefs.getUChar(("m" + String(i)).c_str(), slots[i].m);
    slots[i].on = prefs.getBool(("e" + String(i)).c_str(), false);
  }
  histStore = prefs.getString("hist", "");
}

void saveSlots() {
  for (int i = 0; i < 4; i++) {
    prefs.putUChar(("h" + String(i)).c_str(), slots[i].h);
    prefs.putUChar(("m" + String(i)).c_str(), slots[i].m);
    prefs.putBool(("e" + String(i)).c_str(), slots[i].on);
  }
}

void addHistory(int slot, const String& status) {
  histStore += dateTimeStr() + "," + status + "," + String(slot) + "\n";
  int lines = 0;
  for (unsigned i = 0; i < histStore.length(); i++) if (histStore[i] == '\n') lines++;
  while (lines > 20) {
    int nl = histStore.indexOf('\n');
    if (nl < 0) break;
    histStore = histStore.substring(nl + 1);
    lines--;
  }
  prefs.putString("hist", histStore);
}

String historyJson() {
  String out = "[";
  int start = 0; bool first = true;
  while (start < (int)histStore.length()) {
    int nl = histStore.indexOf('\n', start);
    if (nl < 0) nl = histStore.length();
    String line = histStore.substring(start, nl);
    start = nl + 1;
    int c1 = line.indexOf(','), c2 = line.lastIndexOf(',');
    if (c1 < 0 || c2 <= c1) continue;
    if (!first) out += ",";
    first = false;
    out += "{\"t\":\"" + line.substring(0, c1) + "\",\"s\":\"" + line.substring(c1 + 1, c2) +
           "\",\"slot\":" + line.substring(c2 + 1) + "}";
  }
  return out + "]";
}

// ======================================================================
// 12. PHONE ALERT (ntfy.sh)
// ======================================================================
void notify(const String& title, const String& msg, const char* prio) {
  if (WiFi.status() != WL_CONNECTED) { Serial.println("[NTFY] no Wi-Fi, alert not sent"); return; }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(6000);
  String url = String("https://ntfy.sh/") + NTFY_TOPIC;
  if (http.begin(client, url)) {
    http.addHeader("Title", title);
    http.addHeader("Priority", prio);
    http.addHeader("Tags", "warning,pill");
    int code = http.POST(msg);
    Serial.printf("[NTFY] sent, HTTP %d\n", code);
    http.end();
  }
}

// ======================================================================
// 13. NAVIGATION
// ======================================================================
bool driveForwardCm(float cm) {
  resetEnc();
  uint32_t lastProgress = millis();
  float lastD = 0;
  while (true) {
    pump();
    if (abortReq) { stopMotors(); return false; }
    float dist = travelledCm();
    if (dist >= cm) break;

    if (obstacleNear) {                      // stop and wait until clear
      stopMotors();
      State ps = st; String pt = statusText; String p1 = lcd1, p2 = lcd2;
      setState(ST_OBSTACLE, "Obstacle ahead");
      lcdShow("Obstacle ahead", "Waiting...");
      while (obstacleNear && !abortReq) { pump(); delay(5); }
      if (abortReq) { stopMotors(); return false; }
      setState(ps, pt);
      lcdShow(p1, p2);
      lastProgress = millis();
      continue;
    }

    if (dist > lastD + 0.5f) { lastD = dist; lastProgress = millis(); }
    if (millis() - lastProgress > 4000) {
      stopMotors();
      failReason = "Robot is stuck or the speed sensors are not counting.";
      return false;
    }

    float err = desiredHeading - heading;     // + means we must turn right
    int corr = constrain((int)(KP_HEADING * err), -60, 60);
    drive(DRIVE_PWM + corr, DRIVE_PWM - corr);
    delay(2);
  }
  brake();
  return true;
}

bool turnBy(float angle) {                   // + = right, - = left
  desiredHeading += angle;
  uint32_t start = millis();
  while (millis() - start < 10000) {
    pump();
    if (abortReq) { stopMotors(); return false; }
    float err = desiredHeading - heading;
    if (fabsf(err) <= TURN_TOL_DEG) {
      brake();
      waitMs(150);
      if (fabsf(desiredHeading - heading) <= TURN_TOL_DEG * 1.5f) return true;
      continue;
    }
    int s = constrain((int)(fabsf(err) * 3.0f), TURN_MIN_PWM, TURN_PWM);
    int dir = (err > 0) ? 1 : -1;
    drive(s * dir, -s * dir);                // right turn: left fwd, right back
    delay(2);
  }
  stopMotors();
  Serial.println("[NAV] turn timeout, continuing");
  return true;
}

bool runSteps(bool reverse) {
  int n = sizeof(ROUTE) / sizeof(ROUTE[0]);
  for (int k = 0; k < n; k++) {
    const Step &s = reverse ? ROUTE[n - 1 - k] : ROUTE[k];
    bool ok = (s.type == 'F') ? driveForwardCm(s.value)
                              : turnBy(reverse ? -s.value : s.value);
    if (!ok) return false;
  }
  return true;
}

// ======================================================================
// 14. DELIVERY SEQUENCE
// ======================================================================
void deliveryStopped(const String& text) {
  stopMotors();
  digitalWrite(PIN_BUZZER, LOW);
  setState(ST_STOPPED, text);
  lcdShow("Robot stopped", "Check the app");
  if (failReason.length()) {
    notify("Medibot problem", failReason + " Please check the robot.", "high");
  }
}

void runDelivery(int slot) {
  abortReq = false; failReason = "";
  String slotTxt = (slot >= 0) ? slotStr(slot) : String("manual test");

  setState(ST_DELIVERING, "Preparing");
  lcdShow("Medicine time", "Keep still...");
  calibrateGyro();

  setState(ST_DELIVERING, "Delivering medicine");
  lcdShow("Delivering...", "Going to room");
  if (!runSteps(false)) { deliveryStopped(failReason.length() ? "Drive problem" : "Stopped by caretaker"); return; }

  // ---- at the room: alert and wait for the button ----
  setState(ST_WAITING, "At room - waiting for patient");
  lcdShow("Take your", "medicine");
  uint32_t t0 = millis();
  bool missed = false, pressed = false;
  while (true) {
    pump();
    if (abortReq) { digitalWrite(PIN_BUZZER, LOW); deliveryStopped("Stopped by caretaker"); return; }
    uint32_t el = millis() - t0;
    if (!missed && el >= CONFIRM_TIMEOUT_MS) {
      missed = true;
      setState(ST_MISSED, "MISSED DOSE!");
      lcdShow("MISSED DOSE!", "Press button");
      notify("Medibot: MISSED DOSE", "Medicine NOT confirmed (slot " + slotTxt + "). Please check the patient.", "urgent");
    }
    uint32_t period = missed ? 300 : 1000, on = missed ? 120 : 200;
    digitalWrite(PIN_BUZZER, (millis() % period) < on);
    if (buttonPressed()) { pressed = true; break; }
    if (missed && el > CONFIRM_TIMEOUT_MS + GIVE_UP_MS) break;
    delay(5);
  }
  digitalWrite(PIN_BUZZER, LOW);

  String result = pressed ? (missed ? "taken_late" : "taken") : "missed";
  addHistory(slot, result);
  Serial.printf("[DOSE] logged: %s\n", result.c_str());
  if (pressed) { lcdShow("Thank you!", "Dose recorded"); beepTimes(2); }
  else         { lcdShow("Dose missed", "Returning home"); }
  waitMs(1500);

  // ---- return home ----
  setState(ST_RETURNING, "Returning home");
  lcdShow("Returning home", "Please wait");
  if (!turnBy(180))        { deliveryStopped("Stopped by caretaker"); return; }
  if (!runSteps(true))     { deliveryStopped(failReason.length() ? "Drive problem" : "Stopped by caretaker"); return; }
  if (!turnBy(180))        { deliveryStopped("Stopped by caretaker"); return; }

  stopMotors();
  setState(ST_IDLE, "Waiting at home");
  lcdShow("Delivery done", "At home");
  waitMs(2000);
}

// ======================================================================
// 15. SCHEDULER
// ======================================================================
void checkSchedule() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  if (!timeSynced()) return;
  struct tm t; nowTm(t);
  int key = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
  for (int i = 0; i < 4; i++) {
    if (slots[i].on && slots[i].h == t.tm_hour && slots[i].m == t.tm_min && key != lastKey[i]) {
      lastKey[i] = key;                    // each slot triggers once per minute
      pendingSlot = i;
      startReq = true;
      Serial.printf("[SCHED] slot %d reached\n", i);
      return;
    }
  }
}

void idleDisplay() {
  if (st == ST_STOPPED) { lcdShow("Robot stopped", "Use app Drive"); return; }
  String l1 = timeSynced() ? ("Time " + timeStr()) : String("Time: waiting..");
  String nx = nextDoseStr();
  String l2 = (nx != "--:--") ? ("Next dose " + nx) : String("No dose set");
  lcdShow(l1, l2);
}

void manualDrive() {
  if (millis() < manualUntil && manualCmd != 'S') {
    int s = manualSpeed;
    switch (manualCmd) {
      case 'F': if (obstacleNear) drive(0, 0); else drive(s, s); break;
      case 'B': drive(-s, -s); break;
      case 'L': drive(-s, s); break;
      case 'R': drive(s, -s); break;
      default:  drive(0, 0);
    }
    manualActive = true;
  } else if (manualActive) {
    stopMotors();
    manualActive = false;
  }
}

// ======================================================================
// 16. WEB SERVER + API
// ======================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Medibot</title>
<style>
body{font-family:Arial,sans-serif;margin:0;padding:12px;background:#eef2f5;max-width:520px;margin:auto}
.c{background:#fff;border-radius:12px;padding:14px;margin-bottom:12px;box-shadow:0 1px 4px #0002}
h2,h3{margin:4px 0 10px}pre{background:#111;color:#6f6;padding:8px;border-radius:6px;font-size:18px;margin:8px 0}
button{padding:10px 16px;border:0;border-radius:8px;background:#1565c0;color:#fff;font-size:15px;margin:3px}
button.r{background:#c62828}.d{width:72px;height:60px;font-size:24px;touch-action:none}
#st{font-size:20px;font-weight:bold}.ok{color:#2e7d32}.late{color:#ef6c00}.bad{color:#c62828}
</style></head><body>
<h2>Medibot</h2>
<div class="c" id="card"><div id="st">...</div><pre id="lcd"></pre><div id="info"></div>
<button onclick="fetch('/test')">Deliver now</button><button class="r" onclick="fetch('/api/stop')">Stop</button></div>
<div class="c"><h3>Schedule</h3><div id="sch"></div><button onclick="save()">Save schedule</button></div>
<div class="c"><h3>Drive</h3><div>Speed <input type="range" id="sp" min="90" max="255" value="170"></div>
<div style="text-align:center">
<button class="d" data-d="F">&#9650;</button><br>
<button class="d" data-d="L">&#9664;</button><button class="d" data-d="B">&#9660;</button><button class="d" data-d="R">&#9654;</button></div>
<button onclick="fetch('/api/beep')">Test buzzer</button></div>
<div class="c"><h3>History</h3><div id="hist"></div></div>
<script>
let drv=null,loaded=false;
const $=id=>document.getElementById(id);
function drive(d){fetch('/api/drive?d='+d+'&sp='+$('sp').value).catch(()=>{})}
document.querySelectorAll('[data-d]').forEach(b=>{
 const d=b.dataset.d;
 b.addEventListener('pointerdown',e=>{e.preventDefault();drive(d);drv=setInterval(()=>drive(d),200)});
 const x=e=>{if(drv){clearInterval(drv);drv=null;drive('S')}};
 b.addEventListener('pointerup',x);b.addEventListener('pointerleave',x);b.addEventListener('pointercancel',x);
});
function save(){const p=new URLSearchParams();for(let i=0;i<4;i++){p.append('t'+i,$('t'+i).value||'08:00');p.append('e'+i,$('e'+i).checked?'1':'0')}
 fetch('/save',{method:'POST',body:p}).then(()=>alert('Saved'))}
async function poll(){
 try{const s=await (await fetch('/api/state')).json();
  $('st').textContent=s.status;$('card').style.borderLeft='8px solid '+(s.alert?'#c62828':'#2e7d32');
  $('lcd').textContent=s.l1+'\n'+s.l2;
  $('info').textContent='State: '+s.state+' | Time '+s.time+' | Next '+s.next+' | Dist '+s.dist+' cm';
  if(!loaded){let h='';s.slots.forEach((x,i)=>{h+='Slot '+(i+1)+' <input type="time" id="t'+i+'" value="'+x.t+'"> <input type="checkbox" id="e'+i+'" '+(x.e?'checked':'')+'><br>'});$('sch').innerHTML=h;loaded=true}
  let h='';s.history.slice().reverse().forEach(x=>{const c=x.s=='taken'?'ok':(x.s=='taken_late'?'late':'bad');h+=x.t+' - <b class="'+c+'">'+x.s+'</b><br>'});
  $('hist').innerHTML=h||'No doses yet';
  if(!s.synced)fetch('/api/settime?epoch='+Math.floor(Date.now()/1000));
 }catch(e){$('st').textContent='Cannot reach robot'}}
setInterval(poll,2000);poll();
</script></body></html>
)rawliteral";

String stateJson() {
  String a = lcd1, b = lcd2; a.trim(); b.trim();
  String s; s.reserve(1600);
  s += "{\"state\":\"" + String(stateName(st)) + "\"";
  s += ",\"status\":\"" + statusText + "\"";
  s += ",\"l1\":\"" + a + "\",\"l2\":\"" + b + "\"";
  s += ",\"time\":\"" + timeStr() + "\"";
  s += String(",\"synced\":") + (timeSynced() ? "true" : "false");
  s += ",\"next\":\"" + nextDoseStr() + "\"";
  s += String(",\"alert\":") + (st == ST_MISSED ? "true" : "false");
  s += ",\"ip\":\"" + ipStr() + "\"";
  s += ",\"dist\":" + String((int)lastDist);
  s += ",\"heading\":" + String(heading, 1);
  s += ",\"slots\":[";
  for (int i = 0; i < 4; i++) {
    if (i) s += ",";
    s += "{\"t\":\"" + slotStr(i) + "\",\"e\":" + String(slots[i].on ? 1 : 0) + "}";
  }
  s += "],\"history\":" + historyJson() + "}";
  return s;
}

void handleSave() {
  for (int i = 0; i < 4; i++) {
    String tk = "t" + String(i), ek = "e" + String(i);
    if (server.hasArg(tk)) {
      String v = server.arg(tk);
      int c = v.indexOf(':');
      if (c > 0) {
        int h = v.substring(0, c).toInt(), m = v.substring(c + 1).toInt();
        if (h >= 0 && h < 24 && m >= 0 && m < 60) { slots[i].h = h; slots[i].m = m; }
      }
    }
    if (server.hasArg(ek)) slots[i].on = (server.arg(ek) == "1");
    lastKey[i] = -1;
  }
  saveSlots();
  server.send(200, "text/plain", "OK");
}

void handleTest() {
  if (!ready()) { server.send(409, "text/plain", "busy"); return; }
  pendingSlot = -1; startReq = true;
  server.send(200, "text/plain", "OK");
}

void handleDrive() {
  if (!ready()) { server.send(409, "text/plain", "busy"); return; }
  String d = server.arg("d");
  int sp = server.arg("sp").toInt(); if (sp <= 0) sp = 170;
  manualSpeed = constrain(sp, 80, 255);
  if (d.length() == 0 || d[0] == 'S') { manualCmd = 'S'; manualUntil = 0; }
  else { manualCmd = d[0]; manualUntil = millis() + 500; }   // auto-stops if commands stop
  server.send(200, "text/plain", "OK");
}

void setupServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, []() { server.send(200, "application/json", stateJson()); });
  server.on("/save", HTTP_POST, handleSave);
  server.on("/test", HTTP_GET, handleTest);
  server.on("/api/drive", HTTP_GET, handleDrive);
  server.on("/api/beep", HTTP_GET, []() { beepTimes(2); server.send(200, "text/plain", "OK"); });
  server.on("/api/stop", HTTP_GET, []() {
    abortReq = true; manualUntil = 0; stopMotors();
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/motortest", HTTP_GET, []() {
    if (!ready()) { server.send(409, "text/plain", "busy"); return; }
    motorTestReq = true; server.send(200, "text/plain", "OK");
  });
  server.on("/api/clearhistory", HTTP_GET, []() {
    histStore = ""; prefs.putString("hist", histStore);
    server.send(200, "text/plain", "OK");
  });
  server.on("/api/settime", HTTP_GET, []() {
    if (!timeSynced() && server.hasArg("epoch")) {
      struct timeval tv; tv.tv_sec = (time_t)server.arg("epoch").toInt(); tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
    }
    server.send(200, "text/plain", "OK");
  });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

// ======================================================================
// 17. SERIAL TESTS  (type a letter in the Serial Monitor)
// ======================================================================
void testLcd() {
  Serial.printf("[TEST l] LCD address 0x%02X, ok=%d\n", lcdAddr, lcdOk);
  lcdShow("LCD test OK", "0123456789ABCDEF");
  waitMs(3000);
}
void testBuzzer() { Serial.println("[TEST b] 3 beeps"); beepTimes(3); }
void testButton() {
  Serial.println("[TEST y] press the confirm button within 10 s...");
  uint32_t t = millis();
  while (millis() - t < 10000) {
    if (buttonPressed()) Serial.println("  button PRESSED");
    delay(10);
  }
  Serial.println("  done");
}
void testUltra() {
  Serial.println("[TEST u] distance (cm); 999 = no echo");
  for (int i = 0; i < 20; i++) { Serial.printf("  %.1f\n", readSonarCm()); delay(300); }
}
void testGyro() {
  Serial.printf("[TEST g] mpuOk=%d  bias=%.3f. Rotate the robot by hand (right = positive)\n", mpuOk, gyroBias);
  heading = 0;
  uint32_t t = millis(), p = 0;
  while (millis() - t < 12000) {
    pump();
    if (millis() - p > 300) { p = millis(); Serial.printf("  heading = %.1f deg\n", heading); }
    delay(3);
  }
}
void testEnc() {
  Serial.println("[TEST e] turn each wheel by hand; counts should rise");
  resetEnc();
  uint32_t t = millis();
  while (millis() - t < 10000) {
    noInterrupts(); uint32_t l = encL, r = encR; interrupts();
    Serial.printf("  left=%lu right=%lu\n", (unsigned long)l, (unsigned long)r);
    delay(400);
  }
}
void testMotors() {
  Serial.println("[TEST m] WHEELS MUST BE LIFTED. Each step 1 s.");
  lcdShow("Motor test", "Wheels lifted!");
  struct { const char* n; int l; int r; } seq[] = {
    {"LEFT forward", 170, 0}, {"LEFT backward", -170, 0},
    {"RIGHT forward", 0, 170}, {"RIGHT backward", 0, -170},
    {"BOTH forward", 170, 170}
  };
  for (auto &s : seq) {
    Serial.printf("  %s\n", s.n);
    drive(s.l, s.r); waitMs(1000); stopMotors(); waitMs(500);
  }
  Serial.println("  done. Wrong direction on one wheel -> swap its 2 wires; whole side -> LEFT/RIGHT_INVERT");
}
void printHelp() {
  Serial.println("\n==== MEDIBOT TEST MENU ====");
  Serial.println("l LCD   b buzzer   y button   u ultrasonic");
  Serial.println("g gyro  e speed sensors   m motors (wheels lifted!)");
  Serial.println("t start a test delivery   s stop   i info");
}
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') continue;
    switch (c) {
      case 'l': testLcd(); break;
      case 'b': testBuzzer(); break;
      case 'y': testButton(); break;
      case 'u': testUltra(); break;
      case 'g': testGyro(); break;
      case 'e': testEnc(); break;
      case 'm': testMotors(); break;
      case 't': if (ready()) { pendingSlot = -1; startReq = true; } break;
      case 's': abortReq = true; stopMotors(); break;
      case 'i': Serial.printf("IP %s | time %s | state %s | dist %.0f | mpu %d | lcd %d\n",
                  ipStr().c_str(), timeStr().c_str(), stateName(st), lastDist, mpuOk, lcdOk); break;
      default: printHelp();
    }
  }
}

// ======================================================================
// 18. PUMP (called all the time) + SETUP + LOOP
// ======================================================================
void pump() {
  updateGyro();
  updateSonar();
  server.handleClient();
  static uint32_t lastWifi = 0;
  if (millis() - lastWifi > 30000) {
    lastWifi = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  }
}

void setup() {
  // motors off first
  pinMode(PIN_L_IN1, OUTPUT); pinMode(PIN_L_IN2, OUTPUT);
  pinMode(PIN_R_IN1, OUTPUT); pinMode(PIN_R_IN2, OUTPUT);
  pinMode(PIN_L_EN, OUTPUT);  pinMode(PIN_R_EN, OUTPUT);
  pwmSetup(); stopMotors();
  pinMode(PIN_TRIG, OUTPUT); pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_ENC_L, INPUT); pinMode(PIN_ENC_R, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_L), isrL, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_R), isrR, RISING);

  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== MEDIBOT starting ===");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);
  lcdInit();
  lcdShow("Medibot", "Starting...");

  mpuOk = mpuInit();
  Serial.printf("[MPU6050] %s\n", mpuOk ? "found" : "NOT FOUND (check wiring)");
  lcdShow("Calibrating gyro", "Keep robot still");
  calibrateGyro(500);

  loadAll();

  // ---- Wi-Fi ----
  lcdShow("Connecting WiFi", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] failed, starting own access point");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
  setenv("TZ", TZ_STRING, 1);
  tzset();

  setupServer();
  Serial.printf("[WIFI] robot IP: %s\n", ipStr().c_str());
  lcdShow("Robot IP address", ipStr());
  delay(5000);
  beepTimes(1);
  setState(ST_IDLE, "Waiting at home");
  printHelp();
}

void loop() {
  pump();
  handleSerial();
  if (ready()) {
    abortReq = false;
    manualDrive();
    checkSchedule();
    if (startReq) {
      startReq = false; manualActive = false; stopMotors();
      runDelivery(pendingSlot);
    } else if (motorTestReq) {
      motorTestReq = false;
      testMotors();
    }
    if (ready() && !manualActive) idleDisplay();
  }
  delay(2);
}
