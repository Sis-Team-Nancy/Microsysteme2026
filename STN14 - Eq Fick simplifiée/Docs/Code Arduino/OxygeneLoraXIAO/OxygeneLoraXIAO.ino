#include <Arduino.h>
#include <DFRobot_EOxygenSensor.h>
#include <RadioLib.h>
#include <Wire.h>

//   PINS LORA XIAO 
#define LORA_CS 10
#define LORA_DIO1 1
#define LORA_RST 2
#define LORA_BUSY 3

//  DECLARATIONS GLOBALES 
// On crée les objets ici pour qu'ils soient reconnus dans setup() ET loop()
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
DFRobot_EOxygenSensor_I2C oxygen(&Wire, E_OXYGEN_ADDRESS_0);

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Initialisation Radio
  Serial.print(F("[LoRa] Initialisation... "));
  int state = radio.begin(868.0);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("Succès !"));
    radio.setSpreadingFactor(7);
    radio.setBandwidth(125.0);
  } else {
    Serial.print(F("Échec, code "));
    Serial.println(state);
    while (true); // Bloque ici si la radio ne marche pas
  }

  // Initialisation Capteur O2
  while(!oxygen.begin()){
    Serial.println("Capteur O2 non détecté !");
    delay(1000);
  }
  Serial.println("Capteur O2 prêt.");
}

void loop() {
  // 1. Lecture de la concentration
  float o2 = oxygen.readOxygenConcentration();
  
  // Sécurité si valeur aberrante
  if (o2 <= 0.1) o2 = 20.9; 

  // 2. Préparation du paquet
  String packet = "O2:" + String(o2, 2);
  
  // 3. Envoi
  Serial.print(F("[LoRa] Envoi en cours : "));
  Serial.println(packet);
  
  int state = radio.transmit(packet.c_str());

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("Transmission terminée avec succès."));
  } else {
    Serial.print(F("Erreur de transmission, code "));
    Serial.println(state);
  }

  // 4. Attente avant la prochaine mesure
  delay(2000); 
}