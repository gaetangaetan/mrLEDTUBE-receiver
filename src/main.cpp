#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <espnow.h>
// 1 DMX RECEIVER : 3C:61:05:D1:CC:57 / COM17 serial …BIA
// 2 mrLEDTUBE : 3C:61:05:D3:19:32  COM18 serial 7

int flashInterval;

typedef struct struct_message {
    uint8_t status;
    uint8_t data;    
} struct_message;


struct_message incomingMessage;
struct_message outgoingMessage;


void OnDataSent(u8 *mac_addr, u8 status) {  
     
}


// Callback when data is received
void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));  
  digitalWrite(LED_BUILTIN,LOW);
  delay(100);
  digitalWrite(LED_BUILTIN,HIGH);
  //delay(incomingMessage.data); 
}


void setup() {
pinMode(LED_BUILTIN,OUTPUT);
Serial.begin(115200);
Serial.println("!!! START !!!! mrLEDTUBE receiver");
delay(1000);
  WiFi.disconnect(); 
  ESP.eraseConfig();
 if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
   // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
// REPLACE WITH RECEIVER MAC Address
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t mrDMXRECEIVERAddress[] = {0x3C, 0x61, 0x05, 0xD1, 0xCC, 0x57};
  // Register peer
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

    // Register for a callback function that will be called when data is sent
  esp_now_register_send_cb(OnDataSent);
  
  // Register for a callback function that will be called when data is received
  esp_now_register_recv_cb(OnDataRecv);

}

void loop() {
  // put your main code here, to run repeatedly:
  // Serial.println("mrLEDTUBE receiver");
  // delay(1000);
}

