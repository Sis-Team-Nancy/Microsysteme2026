/////This code was created by two Master’s Year 1 biomedical engineering students, ////
/////as part of a project supervised by the junior enterprise, Sisteam Nancy.//////////
/////STN_11: "Estimation of PaO₂ from  SpO₂" BAH Fatoumata Binta, ZAKRZEWSKI Nathan////
////////////////////////// ©SISTEAM Nancy 2026////////////////////////////////////////


//Library declarations
#include <Arduino.h>
#include <RadioLib.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"

// LoRa pin definitions
#define LORA_SS    8
#define LORA_DIO1  14
#define LORA_RST   12
#define LORA_BUSY  13

// LoRa module declaration
Module loraModule(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY);
SX1262 radio(&loraModule);

// OLED display declaration
SSD1306Wire oled(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// Serial port definition
HardwareSerial MySerial(1);
#define RX_PIN 4
#define TX_PIN 5

// Medical variables declaration
int spo2 = 0;
int pao2 = 0;
int fp = 0;
int temperature = 0;
float humidity = 0;
String str = "";

// Timer variable
unsigned long dernierEnvoiLoRa = 0;
const long intervalleLoRa = 5000;

void setup() {
// Serial communication setup
  Serial.begin(115200);

// Powering on the OLED display
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(100);

// OLED initialization and startup screen
  oled.init();
  oled.screenRotate(ANGLE_90_DEGREE);
  oled.clear();
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, 0, "Projet de :");
  oled.drawString(0, 20, "Bah");
  oled.drawString(0, 40, "Binta");
  oled.drawString(0, 60, "Zakrzewski");
  oled.drawString(0, 80, "Nathan");
  oled.drawString(0, 100, "Master IB1");
  oled.display();
  delay(5000);

// LoRa initialization
  oled.clear();
  oled.drawString(0, 0, "Initialisation TX...");
  oled.display();
  int state = radio.begin(868.0); // LoRa startup with 868 MHz transmission frequency
  if (state == RADIOLIB_ERR_NONE) {
    radio.setBandwidth(125.0); // Bandwidth
    radio.setSpreadingFactor(7); //Spreading factor
    radio.setSyncWord(0x12); // Network identification
  } else {
    while(true); 
  }
// Serial communication setup
  MySerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); // (baud_rate, frame_format(bits, parity, stop), RX_pin, TX_pin)
}

void loop() {
 


// Receiving data on the serial port
    if (MySerial.available())     {
      str = MySerial.readStringUntil('\n');
      str.trim();

// Restarting the timer for LoRa transmission every 5 seconds
  unsigned long tempsActuel = millis();
  if (tempsActuel - dernierEnvoiLoRa >= intervalleLoRa) {
// Frame parsing for OLED display
      if (str.startsWith("S:")) {
        spo2 = str.substring(str.indexOf("S:")+2, str.indexOf(" P:")).toFloat(); 
        pao2 = str.substring(str.indexOf("P:")+2, str.indexOf(" F:")).toFloat(); 
        fp = str.substring(str.indexOf("F:")+2, str.indexOf(" T:")).toFloat(); 
        temperature = str.substring(str.indexOf("T:")+2, str.indexOf(" H:")).toFloat(); 
        humidity = str.substring(str.indexOf("H:")+2, str.indexOf(" S:")).toFloat(); 

      
        if (spo2 <= 0) {
          oled.clear();
          oled.drawString(0, 0, "doigt absent");
          oled.drawString(0, 12, "PAO2 SPO2 FP");
          oled.drawString(0, 24, "absent");
          oled.drawString(15, 36, String(pao2) + " mmHg");
          } else {
              oled.clear();
              oled.drawString(0, 0, "SPO2: " + String(spo2) + " %");
              oled.drawString(0, 12, "PAO2:");
              oled.drawString(15, 24, String(pao2) + " mmHg");
              oled.drawString(0, 36, "FP: " + String(fp) + " bpm");
            }

  // Displaying values on the OLED screen
        

        oled.drawString(0, 48, "Temp:");
        oled.drawString(15, 60, String(temperature) + " C");

        oled.drawString(0, 72,"Hum:");
        oled.drawString(15, 84,String(humidity) + " %");

        oled.drawString(0, 104,"-----------------------------");

        oled.drawString(0, 111,"envoi LoRa");
        oled.display();}


  
    
    if (str.length() > 0 && str.startsWith("S:")) {
        oled.drawString(0, 110, "ENVOI LORA...");
        oled.display();

    // Sending data via LoRa
        int state = radio.transmit(str);

    // Write to the serial port whether the transmission succeeded or not
        if (state == RADIOLIB_ERR_NONE) {
          Serial.println(F("[LoRa] Envoi réussi !"));
        } else {
          Serial.print(F("[LoRa] Erreur d'envoi : "));
          Serial.println(state);
        }}

    dernierEnvoiLoRa = tempsActuel; // Restart timer
  }}
}

