#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

// ===== LoRa =====
#define LORA_CS    41
#define LORA_DIO1  39
#define LORA_RESET 42
#define LORA_BUSY  40
#define LORA_SCK   7
#define LORA_MISO  8
#define LORA_MOSI  9

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RESET, LORA_BUSY);

volatile bool loraDone = true;
void ARDUINO_ISR_ATTR onLoraDone() { loraDone = true; }

// ===== TIMER (250 Hz) =====
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

const uint32_t Fe = 250;
const uint32_t Te_us = 1000000 / Fe;

// ✅ compteur de ticks en attente (évite de perdre des samples)
volatile uint32_t pendingTicks = 0;

void ARDUINO_ISR_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  pendingTicks++;
  portEXIT_CRITICAL_ISR(&timerMux);
}

// ===== ECG (ton algo) =====
const int ECG_PIN = 1;

// Filtres
float hp_mem = 0.0f;
float x_prev = 0.0f;
const float FC_HP = 0.5f;
const float T = 1.0f / Fe;
const float RC_HP = 1.0f / (2.0f * PI * FC_HP);
const float A_HP = RC_HP / (RC_HP + T);

float lp_mem = 0.0f;
const float FC_LP = 20.0f;
const float RC_LP = 1.0f / (2.0f * PI * FC_LP);
const float A_LP = T / (RC_LP + T);

// Seuil adaptatif
float signal_max = 0.0f;
float signal_baseline = 0.0f;
const float ALPHA_MAX = 0.01f;
const float ALPHA_BASE = 0.005f;
float SEUIL_QRS = 30000.0f;

// Détection QRS
float y_prev = 0.0f;
float y_prev2 = 0.0f;

// ✅ refractory abaissé -> OK jusqu'à >200 bpm
const uint32_t REFRACTORY_MS = 220;

uint32_t lastBeatMs = 0;
uint32_t lastBeatUs = 0;

// RR
const int N_RR = 8;
uint32_t rr[N_RR];
int rr_idx = 0;
int rr_count = 0;

// Moyenne BPM
const int N_BPM = 5;
float bpm_history[N_BPM];
int bpm_idx = 0;
int bpm_count = 0;
float currentBPM = 0.0f;

// Debug / send
uint32_t lastDisplayMs = 0;
const uint32_t DISPLAY_INTERVAL_MS = 1000;

uint32_t lastSendMs = 0;
const uint32_t SEND_INTERVAL_MS = 2000;

static char loraMsg[16] = "0";

void processECGtick(uint32_t nowMs) {
  float x = (float)analogRead(ECG_PIN);

  hp_mem = A_HP * (hp_mem + x - x_prev);
  x_prev = x;
  float filtered = hp_mem;

  lp_mem += A_LP * (filtered - lp_mem);

  float y = lp_mem * lp_mem;

  if (y > signal_max) {
    signal_max = ALPHA_MAX * y + (1.0f - ALPHA_MAX) * signal_max;
  } else {
    signal_max = (1.0f - ALPHA_MAX * 0.01f) * signal_max;
  }

  signal_max = constrain(signal_max, 10000.0f, 100000.0f);

  signal_baseline = ALPHA_BASE * y + (1.0f - ALPHA_BASE) * signal_baseline;
  SEUIL_QRS = signal_baseline + 0.6f * (signal_max - signal_baseline);

  bool isPeak = (y_prev > y_prev2) && (y_prev > y) && (y_prev > SEUIL_QRS);
  bool inRefractory = (nowMs - lastBeatMs < REFRACTORY_MS);

  if (isPeak && !inRefractory) {
    uint32_t nowUs = micros();
    uint32_t rr_us = nowUs - lastBeatUs;
    uint32_t rr_ms = rr_us / 1000;

    // bornes RR (tu peux les adapter)
    if (rr_ms > 250 && rr_ms < 1500) {
      lastBeatMs = nowMs;
      lastBeatUs = nowUs;

      rr[rr_idx] = rr_ms;
      rr_idx = (rr_idx + 1) % N_RR;
      if (rr_count < N_RR) rr_count++;

      if (rr_count >= 3) {
        uint32_t rr_sorted[N_RR];
        for (int i = 0; i < rr_count; i++) rr_sorted[i] = rr[i];

        for (int i = 0; i < rr_count - 1; i++) {
          for (int j = 0; j < rr_count - i - 1; j++) {
            if (rr_sorted[j] > rr_sorted[j + 1]) {
              uint32_t tmp = rr_sorted[j];
              rr_sorted[j] = rr_sorted[j + 1];
              rr_sorted[j + 1] = tmp;
            }
          }
        }

        uint32_t rr_median = rr_sorted[rr_count / 2];
        float bpm_instant = 60000.0f / (float)rr_median;

        bpm_history[bpm_idx] = bpm_instant;
        bpm_idx = (bpm_idx + 1) % N_BPM;
        if (bpm_count < N_BPM) bpm_count++;

        float sum = 0.0f;
        for (int i = 0; i < bpm_count; i++) sum += bpm_history[i];
        currentBPM = sum / (float)bpm_count;
      }
    }
  }

  y_prev2 = y_prev;
  y_prev = y;
}

void trySendLoRa(uint32_t nowMs) {
  if (nowMs - lastSendMs < SEND_INTERVAL_MS) return;
  if (!loraDone) return;
  lastSendMs = nowMs;

  int bpmToSend = (int)(currentBPM + 0.5f);
  if (bpmToSend < 0) bpmToSend = 0;

  // ✅ envoi brut "72"
  snprintf(loraMsg, sizeof(loraMsg), "%d", bpmToSend);

  loraDone = false;
  int st = radio.startTransmit(loraMsg);
  if (st != RADIOLIB_ERR_NONE) loraDone = true;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(12);
  pinMode(ECG_PIN, INPUT);

  for (int i = 0; i < N_BPM; i++) bpm_history[i] = 0.0f;

  lastBeatMs = millis();
  lastBeatUs = micros();
  lastDisplayMs = lastBeatMs;
  lastSendMs = lastBeatMs;

  // LoRa init
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  radio.setDio2AsRfSwitch(true);

  int st = radio.begin(868.0, 125.0, 7, 5, 0x12, 14, 8);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.print("LoRa init error: "); Serial.println(st);
    while (1) delay(10);
  }
  radio.setDio1Action(onLoraDone);
  loraDone = true;

  // Timer init (1 MHz)
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, Te_us, true, 0);

  Serial.println("READY");
}

void loop() {
  uint32_t nowMs = millis();

  // ✅ consomme TOUS les ticks accumulés (pas de perte d'échantillons)
  uint32_t ticks = 0;
  portENTER_CRITICAL(&timerMux);
  ticks = pendingTicks;
  pendingTicks = 0;
  portEXIT_CRITICAL(&timerMux);

  // sécurité: si gros retard, on limite pour éviter de bloquer la loop
  if (ticks > 10) ticks = 10;

  for (uint32_t i = 0; i < ticks; i++) {
    processECGtick(nowMs);
  }

  trySendLoRa(nowMs);

  if (nowMs - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayMs = nowMs;
    Serial.print(nowMs / 1000);
    Serial.print("s | ");
    Serial.print((int)(currentBPM + 0.5f));
    Serial.print(" BPM | LoRa: ");
    Serial.println(loraMsg);
  }
}
