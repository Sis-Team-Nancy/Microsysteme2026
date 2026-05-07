/////This code was created by two Master’s Year 1 biomedical engineering students, ////
/////as part of a project supervised by the junior enterprise, Sisteam Nancy.//////////
/////STN_11: "Estimation of PaO₂ from  SpO₂" BAH Fatoumata Binta, ZAKRZEWSKI Nathan////
////////////////////////// ©SISTEAM Nancy 2026////////////////////////////////////////

// Library declarations
#include <SPI.h>
#include "protocentral_afe44xx.h"
#include <DHT.h>

// UART pin definitions
#define HELTEC_RX 16  
#define HELTEC_TX 17  

// AFE4490 pin definitions
#define AFE44XX_CS_PIN   5
#define AFE44XX_PWDN_PIN 4
#define AFE44XX_INTNUM   0

// DHT11 pin definitions
#define DHTPIN 13
#define DHTTYPE DHT22

// Sensor and variable declarations
AFE44XX afe44xx(AFE44XX_CS_PIN, AFE44XX_PWDN_PIN);
afe44xx_data afe44xx_raw_data;
DHT dht(DHTPIN, DHTTYPE);
int32_t heart_rate_prev=0;
int32_t spo2=0;

// PAO2 calculation
float pao2=0;
float n=0.7;
float p50=26.6;

void setup() {     

// Serial communication setup
  Serial2.begin(115200, SERIAL_8N1, HELTEC_RX, HELTEC_TX); 
  
// Initialization of sensors and SPI communication
  Serial.begin(115200);
  Serial.println("Intilaziting AFE44xx.. ");
  SPI.begin();
  afe44xx.afe44xx_init();
  Serial.println("Inited...");
  dht.begin();
}

void loop() {

// Reading data from the DHT11 sensor
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
// DHT11 error handling
  if (isnan(temperature) || isnan(humidity)) {
      temperature = 0;
      humidity = 0;
}
// Reading raw data from the AFE4490 sensor
  afe44xx.get_AFE44XX_Data(&afe44xx_raw_data); 
  
// AFE4490 error handling
  if (afe44xx_raw_data.buffer_count_overflow)
    {
      if(afe44xx_raw_data.spo2 == -999)
      { 
        spo2=0;
        Serial.println("-1");
      }
// Storing data into variables  
      else if ((heart_rate_prev != afe44xx_raw_data.heart_rate) || (spo2 != afe44xx_raw_data.spo2))
      {
        heart_rate_prev = afe44xx_raw_data.heart_rate;
        spo2 = afe44xx_raw_data.spo2;
        pao2= 0 ;//p50*(spo2/(1-spo2))^(1/n);

// Frame creation
  String message = "S:" + String(spo2) + 
                   " P:" + String(pao2) +
                   " F:" + String(heart_rate_prev) +
                   " T:" + String(temperature) +
                   " H:" + String(humidity);

// Send to Heltec every 8 seconds
  Serial2.println(message);   
  delay(8); 
}}}