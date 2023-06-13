#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <espnow.h>
// 1 DMX RECEIVER : 3C:61:05:D1:CC:57 / COM17 serial …BIA
// 2 mrLEDTUBE : 3C:61:05:D3:19:32  COM18 serial 7

int flashInterval;

uint8_t dmxChannels[512];

typedef struct struct_message {
    uint8_t status;
    uint8_t data;    
} struct_message;


struct_message incomingMessage;
struct_message outgoingMessage;

typedef struct struct_dmx_message {
    uint8_t dmx001;
    uint8_t dmx002;
    uint8_t dmx003;
    uint8_t dmx004;
        
} struct_dmx_message;

struct_dmx_message incomingDMXMessage;

typedef struct struct_dmx_packet {
    uint8_t blockNumber; // on divise les 512 adresses en 4 blocs de 128 adresses
    uint8_t dmxvalues[128];   
} struct_dmx_packet;

struct_dmx_packet incomingDMXPacket;


void OnDataSent(u8 *mac_addr, u8 status) {  
     
}


// Callback when data is received
void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&incomingDMXPacket, incomingData, sizeof(incomingDMXPacket));  
  uint8_t packetNumber = incomingDMXPacket.blockNumber;
  for(int i=0;i<128;i++)
  {
    dmxChannels[(packetNumber*128)+i]=incomingDMXPacket.dmxvalues[i];
  }
  //  Serial.print(incomingDMXMessage.dmx001);
  //  Serial.print(" ");
  //  Serial.print(incomingDMXMessage.dmx002);
  //  Serial.print(" ");
  //  Serial.print(incomingDMXMessage.dmx003);
  //  Serial.print(" ");
  //  Serial.print(incomingDMXMessage.dmx004);
  //  Serial.println(" ");
   //delay(50);

  // digitalWrite(LED_BUILTIN,LOW);
  // delay(100);
  // digitalWrite(LED_BUILTIN,HIGH);
  // Serial.println("data received");
  //delay(incomingMessage.data); 
}


void setup() {
pinMode(LED_BUILTIN,OUTPUT);
  Serial.begin(115200);
  WiFi.disconnect();
  ESP.eraseConfig();
 
  // Wifi STA Mode
  WiFi.mode(WIFI_STA);
  // Get Mac Add
  Serial.print("Mac Address: ");
  Serial.print(WiFi.macAddress());
  Serial.println("\nESP-Now Receiver");
  
  // Initializing the ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Problem during ESP-NOW init");
    return;
  }
  
  //esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  // We can register the receiver callback function
  esp_now_register_recv_cb(OnDataRecv);
// esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
// REPLACE WITH RECEIVER MAC Address
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t mrDMXRECEIVERAddress[] = {0x3C, 0x61, 0x05, 0xD1, 0xCC, 0x57};
  // Register peer
  //esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

  
}

void loop() {
    digitalWrite(LED_BUILTIN,LOW);
  delay(5);
  digitalWrite(LED_BUILTIN,HIGH);  
  delay(4*(255-dmxChannels[0])); 

  // put your main code here, to run repeatedly:
  // Serial.println("mrLEDTUBE receiver");
  //  Serial.print(dmxChannels[0]);
  //  Serial.print(" ");
  //  Serial.print(dmxChannels[1]);
  //  Serial.print(" ");
  //  Serial.print(dmxChannels[2]);
  //  Serial.print(" ");
  //  Serial.print(dmxChannels[3]);
  //  Serial.println(" ");
  //  delay(20);
  // delay(1000);
}

