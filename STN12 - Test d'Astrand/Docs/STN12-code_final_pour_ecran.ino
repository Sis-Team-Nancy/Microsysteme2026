#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ===================== TYPES / ENUMS (TOUJOURS EN PREMIER) =====================
struct Rect { int x, y, w, h; };
static inline bool hit(const Rect &r, int x, int y) {
  return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

enum HRMode  { HR_SIM, HR_SERIAL };
enum TestMode{ MODE_ERGO, MODE_TAPIS };

enum State {
  ST_PATIENT,
  ST_REST_OFFER,
  ST_REST_MEASURE,
  ST_SELECT_MODE,
  ST_WARMUP,
  ST_WAIT_TARGET,
  ST_TEST,
  ST_WORKLOAD_INPUT,
  ST_RESULTS
};

// ===================== TFT + TOUCH =====================
TFT_eSPI tft = TFT_eSPI();

#define XPT2046_CS   33
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25

// ---- UART depuis XIAO via connecteur P1 du CYD ----
// UART depuis le XIAO : on reçoit sur IO22 (c'est le seul qui a marché)
#define CYD_RX 22
#define CYD_TX -1              // pas utilisé
HardwareSerial CYDSerial(1);   // peu importe 1 ou 2, mais on garde 1 propre

SPIClass touchscreenSPI(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static const float A = 0.088979f;
static const float B = -0.001556f;
static const float C = 11.658514f;
static const float D = 0.000256f;
static const float E = 0.065550f;
static const float F = 16.086857f;
static const int TS_OFFX = 26;
static const int TS_OFFY = 33;

static inline bool getTouchPixel(int &px, int &py) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  int x = (int)lroundf(A * p.x + B * p.y + C);
  int y = (int)lroundf(D * p.x + E * p.y + F);
  x -= TS_OFFX; y -= TS_OFFY;
  x = constrain(x, 0, tft.width()  - 1);
  y = constrain(y, 0, tft.height() - 1);
  px = x; py = y;
  return true;
}

static inline bool getTouchClick(int &px, int &py) {
  static bool wasDown = false;
  bool down = getTouchPixel(px, py);
  if (down && !wasDown) { wasDown = true; return true; }
  if (!down) wasDown = false;
  return false;
}

// ===================== UI helpers =====================
static inline void drawBtn(const Rect &r, uint16_t bg, uint16_t fg, const String &label, int font=2) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 10, fg);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, r.x + r.w/2, r.y + r.h/2, font);
  tft.setTextDatum(TL_DATUM);
}

static inline String fmtTime(uint32_t sec) {
  char b[8];
  snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)(sec/60), (unsigned long)(sec%60));
  return String(b);
}

// ===================== HR =====================
HRMode hrMode = HR_SERIAL;

// Lecture UART via Serial2 (connecteur P1 du CYD : IO35=RX, IO22=TX)
static int readHRSerial() {
  static int lastBpm   = 0;
  static uint32_t lastRx = 0;

  while (CYDSerial.available()) {
    String line = CYDSerial.readStringUntil('\n');
    line.trim();
    if (line.length()) {
      int v = line.toInt();
      if (v >= 30 && v <= 240) {
        lastBpm = v;
        lastRx  = millis();
      }
    }
  }

  // Si rien recu depuis 3s -> retourne 0
  if (millis() - lastRx > 3000) return 0;
  return lastBpm;
}

static inline int hrMaxEstimated(int age) { return 220 - age; }

static int readHRSimRest() {
  static float bpm = 60.0f;
  bpm += ((float)random(-2, 3)) * 0.05f;
  bpm = constrain(bpm, 58.0f, 64.0f);
  return (int)lroundf(bpm);
}

static int readHRSimExercise(int hrLow, int hrHigh) {
  static float bpm = 75.0f;
  int mid = (hrLow + hrHigh) / 2;
  float target = (mid > 0) ? (float)mid : 140.0f;
  bpm += (target - bpm) * 0.015f;
  bpm += ((float)random(-2, 3)) * 0.03f;
  if (hrLow > 0 && bpm < (float)hrLow) bpm = (float)hrLow;
  bpm = constrain(bpm, 50.0f, 210.0f);
  return (int)lroundf(bpm);
}

static inline void targetRangeKarvonen(int age, int hrRest, int &low, int &high) {
  int hrmax = hrMaxEstimated(age);
  int hrr = max(0, hrmax - hrRest);
  low  = (int)lroundf(hrRest + 0.70f * hrr);
  high = (int)lroundf(hrRest + 0.85f * hrr);
}

// ===================== VO2 equations =====================
static float ageCorrectionFactor(int age) {
  if (age <= 24) return 1.10f;
  if (age <= 34) return 1.00f;
  if (age <= 44) return 0.87f;
  if (age <= 54) return 0.83f;
  if (age <= 64) return 0.78f;
  return 0.75f;
}

static float vo2SubErgo_mlkgmin(int powerW, float massKg) {
  if (massKg <= 0) return NAN;
  return (10.8f * ((float)powerW / massKg)) + 7.0f;
}

static float vo2SubTread_mlkgmin(float speedKmh, float gradePct) {
  float v = speedKmh * 1000.0f / 60.0f;
  float g = gradePct / 100.0f;
  if (v < 134.0f) return 0.1f*v + 1.8f*v*g + 3.5f;
  else            return 0.2f*v + 0.9f*v*g + 3.5f;
}

static float vo2MaxFromHRR_mlkgmin(float vo2Sub, int age, int hrRest, int hrSub, bool useAgeCorr=true) {
  if (!isfinite(vo2Sub) || hrRest <= 0 || hrSub <= 0) return NAN;
  int hrmax = hrMaxEstimated(age);
  int num = hrmax - hrRest;
  int den = hrSub - hrRest;
  if (num <= 0 || den <= 0) return NAN;
  float vo2max = vo2Sub * ((float)num / (float)den);
  if (useAgeCorr) vo2max *= ageCorrectionFactor(age);
  return vo2max;
}

// ===================== APP DATA =====================
State    state    = ST_PATIENT;
TestMode testMode = MODE_ERGO;

int   ageYears = 21;
char  sex      = 'M';
float massKg   = 70.0f;

int hrRest = 0;
int hrLow  = 0, hrHigh = 0;

static const uint32_t TEST_SEC_CONST = 360;
static const uint32_t LAST_SEC       = 120;
int  hrBuf[LAST_SEC];
int  hrIdx  = 0;
bool hrFull = false;

static inline void resetHrBuf() {
  for (int i = 0; i < (int)LAST_SEC; i++) hrBuf[i] = 0;
  hrIdx = 0; hrFull = false;
}
static inline void pushHr(int bpm) {
  hrBuf[hrIdx] = bpm;
  hrIdx++;
  if (hrIdx >= (int)LAST_SEC) { hrIdx = 0; hrFull = true; }
}
static inline int avgLast2min() {
  int count = hrFull ? (int)LAST_SEC : hrIdx;
  if (count <= 0) return 0;
  long sum = 0; int n = 0;
  for (int i = 0; i < count; i++) {
    int v = hrBuf[i];
    if (v >= 30 && v <= 240) { sum += v; n++; }
  }
  return (n > 0) ? (int)lround((double)sum / (double)n) : 0;
}

int   warmupMin = 3;
int   powerW    = 125;
float speedKmh  = 8.0f;
float gradePct  = 1.0f;

uint32_t stateStartMs = 0;
uint32_t testStartMs  = 0;
uint32_t lastSampleMs = 0;

// ===================== NAV =====================
static inline void gotoState(int s) {
  state = (State)s;
  stateStartMs = millis();
  tft.fillScreen(TFT_BLACK);
}
static inline void goBack(int s) {
  state = (State)s;
  stateStartMs = millis();
  tft.fillScreen(TFT_BLACK);
}

// ===================== UI RECTS =====================
Rect BTN_BACK  = { 10,  6,  70, 28 };
Rect BTN_MAIN  = { 10, 200, 300, 20 };
Rect BTN_A     = { 10, 204, 145, 18 };
Rect BTN_B     = { 165,204, 145, 18 };
Rect BTN_MINUS = { 10, 220,  90, 20 };
Rect BTN_NEXT  = { 115,220,  90, 20 };
Rect BTN_PLUS  = { 220,220,  90, 20 };

// ===================== COMMON HEADER =====================
static void headerCommon(int bpm) {
  tft.fillRect(0, 0, tft.width(), 40, TFT_BLACK);
  drawBtn(BTN_BACK, TFT_DARKGREY, TFT_WHITE, "< RET", 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Astrand / Submax VO2max", 90, 10, 2);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("FC:", 285, 10, 2);
  tft.drawNumber(bpm, 315, 10, 2);
  tft.setTextDatum(TL_DATUM);
}

// ===================== PATIENT SCREEN =====================
enum FieldP { P_AGE, P_SEX, P_MASS, P_WARMUP, P_MODE };
FieldP fieldP = P_AGE;

static void drawPatientScreen(int bpm) {
  headerCommon(bpm);

  if (hrRest > 0) {
    targetRangeKarvonen(ageYears, hrRest, hrLow, hrHigh);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("FC repos: " + String(hrRest) + " bpm", 10, 45, 2);
    tft.drawString("Cible: " + String(hrLow) + "-" + String(hrHigh) + " (Karvonen)", 10, 65, 2);
  } else {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("FC repos: non mesuree", 10, 45, 2);
  }

  auto row = [&](int y, FieldP f, const String &name, const String &val) {
    uint16_t bg = (fieldP == f) ? TFT_DARKGREY : TFT_BLACK;
    tft.fillRoundRect(10, y, 300, 28, 8, bg);
    tft.drawRoundRect(10, y, 300, 28, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(name, 18, y+6, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(val, 305, y+6, 2);
    tft.setTextDatum(TL_DATUM);
  };

  row(60,  P_AGE,    "Age",     String(ageYears) + " ans");
  row(88,  P_SEX,    "Sexe",    String(sex));
  row(116, P_MASS,   "Poids",   String(massKg, 1) + " kg");
  row(144, P_WARMUP, "Warmup",  String(warmupMin) + " min");
  row(172, P_MODE,   "Mode FC", (hrMode == HR_SIM) ? "SIM" : "SERIAL");

  drawBtn(BTN_MINUS, TFT_NAVY,      TFT_WHITE, "-",        4);
  drawBtn(BTN_NEXT,  TFT_DARKGREEN, TFT_WHITE, "SUIV.",    2);
  drawBtn(BTN_PLUS,  TFT_NAVY,      TFT_WHITE, "+",        4);
  drawBtn(BTN_MAIN,  TFT_MAROON,    TFT_WHITE, "CONTINUER",2);
}

static void incPatient() {
  switch (fieldP) {
    case P_AGE:    ageYears  = min(ageYears + 1, 99);      break;
    case P_SEX:    sex       = (sex == 'M') ? 'F' : 'M';   break;
    case P_MASS:   massKg    = min(massKg + 0.5f, 200.0f); break;
    case P_WARMUP: warmupMin = min(warmupMin + 1, 15);      break;
    case P_MODE:   hrMode    = (hrMode == HR_SIM) ? HR_SERIAL : HR_SIM; break;
  }
}
static void decPatient() {
  switch (fieldP) {
    case P_AGE:    ageYears  = max(ageYears - 1, 10);      break;
    case P_MASS:   massKg    = max(massKg - 0.5f, 30.0f);  break;
    case P_WARMUP: warmupMin = max(warmupMin - 1, 0);       break;
    default: break;
  }
}
static void nextPatientField() {
  if      (fieldP == P_AGE)    fieldP = P_SEX;
  else if (fieldP == P_SEX)    fieldP = P_MASS;
  else if (fieldP == P_MASS)   fieldP = P_WARMUP;
  else if (fieldP == P_WARMUP) fieldP = P_MODE;
  else                          fieldP = P_AGE;
}

// ===================== REST HR =====================
static int      restSamples = 0;
static long     restSum     = 0;
static uint32_t restStartMs = 0;
static const uint32_t REST_MEAS_SEC = 60;

static void drawRestOffer(int bpm) {
  headerCommon(bpm);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Mesurer la FC au repos (1 min) ?", 160, 85, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Recommande pour calcul cible (Karvonen)", 160, 110, 2);
  drawBtn(BTN_A, TFT_DARKGREEN, TFT_WHITE, "OUI",    2);
  drawBtn(BTN_B, TFT_DARKGREY,  TFT_WHITE, "PASSER", 2);
}

static void drawRestMeasure(int bpm) {
  headerCommon(bpm);
  uint32_t elapsed = (millis() - restStartMs) / 1000;
  uint32_t rem = (elapsed >= REST_MEAS_SEC) ? 0 : (REST_MEAS_SEC - elapsed);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Mesure FC repos", 160, 70, 4);
  tft.drawString("Temps: " + fmtTime(rem), 160, 115, 4);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Reste immobile, respiration calme", 160, 150, 2);
  drawBtn(BTN_MAIN, TFT_MAROON, TFT_WHITE, "ANNULER", 2);
}

// ===================== MODE SELECT =====================
static void drawSelectMode(int bpm) {
  headerCommon(bpm);
  if (hrRest > 0) targetRangeKarvonen(ageYears, hrRest, hrLow, hrHigh);
  else {
    int hrmax = hrMaxEstimated(ageYears);
    hrLow  = (int)lroundf(0.70f * hrmax);
    hrHigh = (int)lroundf(0.85f * hrmax);
  }
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Choisir le protocole", 160, 80, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Cible FC: " + String(hrLow) + "-" + String(hrHigh), 160, 110, 2);
  drawBtn(BTN_A, TFT_DARKGREEN, TFT_WHITE, "ERGOCYCLE", 2);
  drawBtn(BTN_B, TFT_DARKGREEN, TFT_WHITE, "TAPIS",     2);
}

// ===================== WARMUP / WAIT / TEST =====================
static void drawWarmup(int bpm) {
  headerCommon(bpm);
  uint32_t elapsed = (millis() - stateStartMs) / 1000;
  uint32_t total   = (uint32_t)warmupMin * 60;
  uint32_t rem     = (elapsed >= total) ? 0 : (total - elapsed);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Echauffement", 160, 85, 4);
  tft.drawString(fmtTime(rem),   160, 130, 4);
  drawBtn(BTN_MAIN, TFT_DARKGREEN, TFT_WHITE, "PASSER", 2);
  if (rem == 0) gotoState(ST_WAIT_TARGET);
}

static void drawWaitTarget(int bpm) {
  headerCommon(bpm);
  bool ok = (bpm >= hrLow && bpm <= hrHigh);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Attendre FC cible", 160, 80, 2);
  tft.setTextColor(ok ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  tft.drawString(ok ? "OK" : "PAS ENCORE", 160, 120, 4);
  drawBtn(BTN_MAIN, ok ? TFT_DARKGREEN : TFT_DARKGREY, TFT_WHITE, "START TEST 6:00", 2);
}

static void drawTest(int bpm) {
  headerCommon(bpm);
  uint32_t elapsed = (millis() - testStartMs) / 1000;
  uint32_t rem     = (elapsed >= TEST_SEC_CONST) ? 0 : (TEST_SEC_CONST - elapsed);

  uint32_t now = millis();
  if (lastSampleMs == 0 || (now - lastSampleMs) >= 1000) {
    lastSampleMs = now;
    pushHr((bpm >= 30 && bpm <= 240) ? bpm : 0);
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TEST 6:00", 160, 75, 4);
  tft.drawString(fmtTime(rem), 160, 125, 6);
  drawBtn(BTN_MAIN, TFT_MAROON, TFT_WHITE, "STOP", 2);

  if (rem == 0) gotoState(ST_WORKLOAD_INPUT);
}

// ===================== WORKLOAD INPUT =====================
enum FieldW { W_POWER, W_SPEED, W_GRADE };
FieldW fieldW = W_POWER;

static void drawWorkload(int bpm) {
  headerCommon(bpm);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Fin test : entrer la charge", 10, 50, 2);

  if (testMode == MODE_ERGO) {
    uint16_t bgP = (fieldW == W_POWER) ? TFT_DARKGREY : TFT_BLACK;
    tft.fillRoundRect(10, 90, 300, 40, 8, bgP);
    tft.drawRoundRect(10, 90, 300, 40, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, bgP);
    tft.drawString("Puissance (W)", 18, 102, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(powerW), 305, 102, 2);
    tft.setTextDatum(TL_DATUM);
  } else {
    uint16_t bgS = (fieldW == W_SPEED) ? TFT_DARKGREY : TFT_BLACK;
    uint16_t bgG = (fieldW == W_GRADE) ? TFT_DARKGREY : TFT_BLACK;

    tft.fillRoundRect(10, 80, 300, 34, 8, bgS);
    tft.drawRoundRect(10, 80, 300, 34, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, bgS);
    tft.drawString("Vitesse (km/h)", 18, 89, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(speedKmh, 1), 305, 89, 2);

    tft.setTextDatum(TL_DATUM);
    tft.fillRoundRect(10, 125, 300, 34, 8, bgG);
    tft.drawRoundRect(10, 125, 300, 34, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, bgG);
    tft.drawString("Inclinaison (%)", 18, 134, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(gradePct, 1), 305, 134, 2);
    tft.setTextDatum(TL_DATUM);
  }

  drawBtn(BTN_MINUS, TFT_NAVY,      TFT_WHITE, "-",              4);
  drawBtn(BTN_NEXT,  TFT_DARKGREEN, TFT_WHITE, "SUIV.",          2);
  drawBtn(BTN_PLUS,  TFT_NAVY,      TFT_WHITE, "+",              4);
  drawBtn(BTN_MAIN,  TFT_DARKGREEN, TFT_WHITE, "CALCULER VO2MAX",2);
}

static void incWorkload() {
  if (testMode == MODE_ERGO) powerW = min(powerW + 5, 600);
  else {
    if      (fieldW == W_SPEED) speedKmh = min(speedKmh + 0.1f, 25.0f);
    else if (fieldW == W_GRADE) gradePct = min(gradePct + 0.5f, 25.0f);
  }
}
static void decWorkload() {
  if (testMode == MODE_ERGO) powerW = max(powerW - 5, 25);
  else {
    if      (fieldW == W_SPEED) speedKmh = max(speedKmh - 0.1f, 1.0f);
    else if (fieldW == W_GRADE) gradePct = max(gradePct - 0.5f, 0.0f);
  }
}
static void nextWorkloadField() {
  if (testMode == MODE_ERGO) fieldW = W_POWER;
  else fieldW = (fieldW == W_SPEED) ? W_GRADE : W_SPEED;
}

// ===================== RESULTS =====================
static void drawResults(int bpm) {
  headerCommon(bpm);

  int   hrSub  = avgLast2min();
  float vo2Sub = (testMode == MODE_ERGO)
                   ? vo2SubErgo_mlkgmin(powerW, massKg)
                   : vo2SubTread_mlkgmin(speedKmh, gradePct);

  int   hrRestUsed = (hrRest > 0) ? hrRest : 70;
  float vo2max = vo2MaxFromHRR_mlkgmin(vo2Sub, ageYears, hrRestUsed, hrSub, true);
  float vo2L   = isnan(vo2max) ? NAN : (vo2max * massKg / 1000.0f);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("FC repos:", 10, 55, 2);
  tft.drawString(String(hrRestUsed) + " bpm", 75, 55, 2);
  tft.drawString("FC submax (2 dernieres min):", 10, 80, 2);
  tft.drawString(String(hrSub) + " bpm", 200, 80, 2);
  tft.drawString("VO2 submax:", 10, 105, 2);
  tft.drawString(String(vo2Sub, 1) + " ml/kg/min", 90, 105, 2);

  if (isnan(vo2max) || hrSub <= 0) {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("Impossible de calculer (FC/charge).", 10, 135, 2);
  } else {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("VO2max estimee:", 10, 135, 2);
    tft.setTextDatum(MC_DATUM);
    tft.drawFloat(vo2max, 1, 165, 150, 6);
    tft.drawString("ml/kg/min", 255, 155, 2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Soit: " + String(vo2L, 2) + " L/min", 10, 185, 2);
  }

  drawBtn(BTN_MAIN, TFT_DARKGREEN, TFT_WHITE, "NOUVEAU TEST", 2);
}

// ===================== BPM DISPATCH =====================
static int computeBpm() {
  if (hrMode == HR_SERIAL) return readHRSerial();
  if (state  == ST_REST_MEASURE) return readHRSimRest();
  int low = hrLow, high = hrHigh;
  if (low <= 0 || high <= 0) {
    int hrmax = hrMaxEstimated(ageYears);
    low  = (int)lroundf(0.70f * hrmax);
    high = (int)lroundf(0.85f * hrmax);
  }
  return readHRSimExercise(low, high);
}

// ===================== INIT TFT =====================
void initTFT() {
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  
  tft.init();
  
  tft.writecommand(0x01); // Software reset
  delay(150);
  
  tft.writecommand(0x11); // Sleep out
  delay(150);
  
  tft.writecommand(0x2A);
  tft.writedata(0x00);
  tft.writedata(0x00);
  tft.writedata(0x01);
  tft.writedata(0x3F);
  
  tft.writecommand(0x2B);
  tft.writedata(0x00);
  tft.writedata(0x00);
  tft.writedata(0x00);
  tft.writedata(0xEF);
  
  tft.writecommand(0x29);
  delay(150);
  
  tft.setRotation(1);  // D'abord setRotation
  
  // MADCTL APRES setRotation pour forcer le bon sens
  tft.writecommand(0x36);
  tft.writedata(0x68);  // ← teste 0x28, 0x48, 0x68, 0x88, 0xA8, 0xC8, 0xE8
  
  tft.fillScreen(TFT_BLACK);
}

// ===================== ARDUINO ENTRYPOINTS =====================
void setup() {
  // UART hardware via connecteur P1 du CYD : IO35=RX, IO22=TX
  CYDSerial.begin(115200, SERIAL_8N1, CYD_RX, CYD_TX);
  delay(200);
  randomSeed(esp_random());

  initTFT();

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchscreenSPI);
  ts.setRotation(1);

  state        = ST_PATIENT;
  stateStartMs = millis();
}

void loop() {
  int bpm = computeBpm();

  int  tx, ty;
  bool click = getTouchClick(tx, ty);

  static uint32_t lastRedraw = 0;
  uint32_t now = millis();
  bool refresh = (now - lastRedraw) > 120;
  if (refresh) lastRedraw = now;

  if (click && hit(BTN_BACK, tx, ty)) {
    switch (state) {
      case ST_PATIENT:        break;
      case ST_REST_OFFER:     goBack(ST_PATIENT);        break;
      case ST_REST_MEASURE:   goBack(ST_REST_OFFER);     break;
      case ST_SELECT_MODE:    goBack(ST_PATIENT);        break;
      case ST_WARMUP:         goBack(ST_SELECT_MODE);    break;
      case ST_WAIT_TARGET:    goBack(ST_WARMUP);         break;
      case ST_TEST:           goBack(ST_WAIT_TARGET);    break;
      case ST_WORKLOAD_INPUT: goBack(ST_TEST);           break;
      case ST_RESULTS:        goBack(ST_WORKLOAD_INPUT); break;
    }
    return;
  }

  switch (state) {

    case ST_PATIENT: {
      if (click) {
        if      (hit(BTN_MINUS, tx, ty)) decPatient();
        else if (hit(BTN_PLUS,  tx, ty)) incPatient();
        else if (hit(BTN_NEXT,  tx, ty)) nextPatientField();
        else if (hit(BTN_MAIN,  tx, ty)) gotoState(ST_REST_OFFER);
        else {
          if      (ty >= 95  && ty < 121) fieldP = P_AGE;
          else if (ty >= 123 && ty < 149) fieldP = P_SEX;
          else if (ty >= 151 && ty < 177) fieldP = P_MASS;
          else if (ty >= 179 && ty < 205) fieldP = P_WARMUP;
          else if (ty >= 207 && ty < 240) fieldP = P_MODE;
        }
      }
      if (refresh) drawPatientScreen(bpm);
      break;
    }

    case ST_REST_OFFER: {
      if (click) {
        if (hit(BTN_A, tx, ty)) {
          restSamples = 0; restSum = 0;
          restStartMs = millis();
          gotoState(ST_REST_MEASURE);
        } else if (hit(BTN_B, tx, ty)) {
          int hrmax = hrMaxEstimated(ageYears);
          hrLow  = (int)lroundf(0.70f * hrmax);
          hrHigh = (int)lroundf(0.85f * hrmax);
          gotoState(ST_SELECT_MODE);
        }
      }
      if (refresh) drawRestOffer(bpm);
      break;
    }

    case ST_REST_MEASURE: {
      static uint32_t lastAcc = 0;
      if (millis() - lastAcc > 200) {
        lastAcc = millis();
        if (bpm >= 30 && bpm <= 240) { restSum += bpm; restSamples++; }
      }
      uint32_t elapsed = (millis() - restStartMs) / 1000;
      if (elapsed >= REST_MEAS_SEC) {
        hrRest = (restSamples > 0) ? (int)lround((double)restSum / (double)restSamples) : 0;
        if (hrRest > 0) targetRangeKarvonen(ageYears, hrRest, hrLow, hrHigh);
        gotoState(ST_SELECT_MODE);
      }
      if (click && hit(BTN_MAIN, tx, ty)) { hrRest = 0; gotoState(ST_SELECT_MODE); }
      if (refresh) drawRestMeasure(bpm);
      break;
    }

    case ST_SELECT_MODE: {
      if (click) {
        if      (hit(BTN_A, tx, ty)) { testMode = MODE_ERGO;  gotoState(ST_WARMUP); }
        else if (hit(BTN_B, tx, ty)) { testMode = MODE_TAPIS; gotoState(ST_WARMUP); }
      }
      if (refresh) drawSelectMode(bpm);
      break;
    }

    case ST_WARMUP: {
      if (click && hit(BTN_MAIN, tx, ty)) gotoState(ST_WAIT_TARGET);
      if (refresh) drawWarmup(bpm);
      break;
    }

    case ST_WAIT_TARGET: {
      if (click && hit(BTN_MAIN, tx, ty)) {
        resetHrBuf();
        testStartMs  = millis();
        lastSampleMs = 0;
        gotoState(ST_TEST);
      }
      if (refresh) drawWaitTarget(bpm);
      break;
    }

    case ST_TEST: {
      if (click && hit(BTN_MAIN, tx, ty)) gotoState(ST_WORKLOAD_INPUT);
      if (refresh) drawTest(bpm);
      break;
    }

    case ST_WORKLOAD_INPUT: {
      if (click) {
        if      (hit(BTN_MINUS, tx, ty)) decWorkload();
        else if (hit(BTN_PLUS,  tx, ty)) incWorkload();
        else if (hit(BTN_NEXT,  tx, ty)) nextWorkloadField();
        else if (hit(BTN_MAIN,  tx, ty)) gotoState(ST_RESULTS);
        else {
          if (testMode == MODE_ERGO) fieldW = W_POWER;
          else {
            if      (ty >= 80  && ty < 115) fieldW = W_SPEED;
            else if (ty >= 125 && ty < 160) fieldW = W_GRADE;
          }
        }
      }
      if (refresh) drawWorkload(bpm);
      break;
    }

    case ST_RESULTS: {
      if (click && hit(BTN_MAIN, tx, ty)) {
        hrIdx = 0; hrFull = false;
        gotoState(ST_PATIENT);
      }
      if (refresh) drawResults(bpm);
      break;
    }
  }

  delay(5);
}
