#include <Arduino.h>
#include <SensirionI2CSfm3000.h>
#include <SensirionI2cStc3x.h>
#include <Wire.h>
#include <SSD1306Wire.h>
#include <RadioLib.h>

// --- CONFIGURATION ECRAN ---
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21 
SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL); 

// --- CONFIGURATION LORA (HELTEC V3) ---
#define LORA_CS 8
#define LORA_DIO1 14
#define LORA_RST 12
#define LORA_BUSY 13
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

// --- CAPTEURS ---
SensirionI2CSfm3000 sfm;
SensirionI2cStc3x stc;

// --- VARIABLES ---
float flow_val = 0, VE = 0.0, VO2 = 0.0, VCO2 = 0.0;
float o2Concentration = 20.9; // Reçue par LoRa depuis le XIAO
float co2Concentration = 0.0, temperature = 0.0;
unsigned long lastSendTime = 0;
float QR = 0.0;
const int sendInterval = 2000; // Un peu plus long pour laisser le XIAO parler

void setup() {
  Serial.begin(115200);
  
  // 1. ALLUMAGE VEXT
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); 
  delay(100);

  // 2. INITIALISATION ÉCRAN
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(50);
  digitalWrite(OLED_RST, HIGH);
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "Initialisation...");
  display.display();

  // 3. INITIALISATION LORA
  int state = radio.begin(868.0); 
  if (state == RADIOLIB_ERR_NONE) {
    radio.setSpreadingFactor(7);
    radio.setBandwidth(125.0);
    radio.startReceive(); // Se met en écoute de l'O2 du XIAO
  }

  // 4. INITIALISATION CAPTEURS LOCAUX
  Wire1.begin(45, 46);
  sfm.begin(Wire1, SFM300_I2C_ADDRESS_0);
  sfm.startContinuousMeasurement();
  stc.begin(Wire1, STC31_C_I2C_ADDR_29);
  stc.setBinaryGas(19); 
}

void loop() {
  // --- A. RECEPTION LORA (O2 du XIAO) ---
  String rxData;
  int rxState = radio.readData(rxData);
  if (rxState == RADIOLIB_ERR_NONE) {
    if (rxData.startsWith("O2:")) {
      o2Concentration = rxData.substring(3).toFloat();
    }
    radio.startReceive(); // Repasse en écoute immédiate
  }

  // --- B. LECTURE ET CALCULS ---
  sfm.readMeasurement(flow_val, 140.0, 32000);
  stc.measureGasConcentration(co2Concentration, temperature);
  co2Concentration = abs(co2Concentration);
  
  VE = (flow_val); 
  VO2 = abs(VE * (0.2093 - (o2Concentration / 100.0)) * 1000.0);
  VCO2 = abs(VE * ((co2Concentration / 100.0) - 0.0004) * 1000.0);
  
  if (VO2 > 0) {
    QR = VCO2 / VO2;
  } else {
    QR = 0.0;
  }
  
  if (VO2 < 0) VO2 = 0; 
  if (VCO2 < 0) VCO2 = 0;

  // --- C. ENVOI LORA (TRÈME FINALE) ---
  if (millis() - lastSendTime > sendInterval) {
    String packet = "A" + String(VO2, 0) + 
                    ",B" + String(VCO2, 0) + 
                    ",C" + String(VE, 1) + 
                    ",D" + String(o2Concentration, 1) + 
                    ",E" + String(co2Concentration, 1) +
                    ",F" + String(QR, 2);
    
    radio.transmit(packet.c_str());
    radio.startReceive(); // Repasse en écoute après l'envoi
    lastSendTime = millis();
  }

// --- D. AFFICHAGE ---
  display.clear();
  
  // Cadre extérieur pour le style
  display.drawRect(0, 0, 128, 64);
  
  // Colonne de GAUCHE : Débits et Volumes
  display.setFont(ArialMT_Plain_10);
  display.drawString(5, 4,  "QR:   " + String(QR, 2));
  display.drawLine(2, 16, 70, 16); // Petite séparation sous le QR
  
  display.drawString(5, 19, "VE:   " + String(VE, 1) + " L/m");
  display.drawString(5, 32, "VO2:  " + String(VO2, 0) + " mL");
  display.drawString(5, 45, "VCO2: " + String(VCO2, 0) + " mL");

  // Séparation verticale
  display.drawLine(75, 0, 75, 64);

  // Colonne de DROITE : Concentrations Temps Réel
  display.drawString(80, 4,  "GAZ (%)");
  display.setFont(ArialMT_Plain_10);
  
  display.drawString(80, 20, "O2:");
  display.drawString(80, 30, String(o2Concentration, 1));
  
  display.drawString(80, 42, "CO2:");
  display.drawString(80, 52, String(co2Concentration, 1));
  
  display.display();
  delay(10); // Réduit pour ne pas bloquer la réception LoRa
}