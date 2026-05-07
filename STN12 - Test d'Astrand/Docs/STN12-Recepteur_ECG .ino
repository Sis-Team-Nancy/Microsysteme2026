/*********************************************************
 * RX LoRa P2P SX1262 (RadioLib) - XIAO ESP32-S3 + WIO-SX1262
 * Reçoit un message ASCII: "72"
 * Affiche BPM reçue + timeout
 *********************************************************/

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

// Pins WIO-SX1262
#define LORA_CS    41
#define LORA_DIO1  39
#define LORA_RESET 42
#define LORA_BUSY  40

#define LORA_SCK   7
#define LORA_MISO  8
#define LORA_MOSI  9

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RESET, LORA_BUSY);

// Flag IRQ
volatile bool packetReceived = false;

#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  packetReceived = true;
}

// Valeur FC reçue
int bpm_rx = -1;
uint32_t last_rx_ms = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== RX LoRa (IRQ) ===");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  // Reset matériel SX1262
  pinMode(LORA_RESET, OUTPUT);
  digitalWrite(LORA_RESET, LOW);
  delay(10);
  digitalWrite(LORA_RESET, HIGH);
  delay(10);

  radio.setDio2AsRfSwitch(true);

  int st = radio.begin(868.0, 125.0, 7, 5, 0x12, 14, 8);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.print("LoRa init error: ");
    Serial.println(st);
    while (1) delay(10);
  }

  // IRQ sur DIO1
  radio.setDio1Action(setFlag);

  // Lancer RX
  st = radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    Serial.print("startReceive error: ");
    Serial.println(st);
    while (1) delay(10);
  }

  Serial.println("RX prêt ✅");
}

void loop() {
  // Si paquet reçu
  if (packetReceived) {
    packetReceived = false;

    String str;
    int st = radio.readData(str);

    if (st == RADIOLIB_ERR_NONE) {
      str.trim();

      Serial.print("RAW RX: '");
      Serial.print(str);
      Serial.println("'");

      int v = str.toInt();
      if (v >= 30 && v <= 240) {
        bpm_rx = v;
        last_rx_ms = millis();
        Serial.print("✅ BPM reçue = ");
        Serial.println(bpm_rx);
      } else {
        Serial.println("⚠️ Valeur hors plage");
      }

    } else {
      Serial.print("readData error: ");
      Serial.println(st);
    }

    // Relance RX
    radio.startReceive();
  }

  // Timeout: si pas reçu depuis 3s
  if (bpm_rx != -1 && (millis() - last_rx_ms) > 3000) {
    bpm_rx = -1;
    Serial.println("⏳ timeout -> BPM = --");
  }
}
