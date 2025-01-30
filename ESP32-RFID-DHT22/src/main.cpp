#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include "DHT.h"
#include <Adafruit_Sensor.h>

// Define pins for SPI (RFID) and DHT22
#define RST_PIN 0   // Reset pin for RFID
#define SS_PIN 21   // NSS pin for RFID (SDA)
#define DHTPIN 17   // DHT22 Data pin
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Define board ID
#define BOARD_ID 1

// WiFi Access Point SSID
const char *ssid = "iot";

// MAC Address of the sink ESP32
uint8_t broadcastAddress[] = {0xEC, 0x62, 0x60, 0x10, 0xA7, 0x84};
esp_now_peer_info_t peerInfo;

// Create an MFRC522 instance
MFRC522 rfid(SS_PIN, RST_PIN);

// Data structure for ESP-NOW communication
typedef struct struct_mote2sinkMessage
{
  int boardId;
  int readingId;
  int timeTag;
  char rfidTag[16];  // RFID UID
  char tagName[32];  // RFID Name
  float temperature;
  float humidity;
} struct_mote2sinkMessage;
struct_mote2sinkMessage espNow_moteData;

// Mapping function for RFID UIDs
const char *getTagName(const char *uid)
{
  if (strcmp(uid, "9BA33B50") == 0)
    return "Doliprane";
  else if (strcmp(uid, "ABB28950") == 0)
    return "Dafalgan";
  else if (strcmp(uid, "442CBE50") == 0)
    return "Lasilix";
  else if (strcmp(uid, "843C0E5B") == 0)
    return "Augmentin";
  else
    return "Pas Défini"; // Default name
}

// ESP-NOW send callback
void espNowOnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

int32_t getWiFiChannel(const char *ssid)
{
  if (int32_t n = WiFi.scanNetworks())
  {
    for (uint8_t i = 0; i < n; i++)
    {
      if (!strcmp(ssid, WiFi.SSID(i).c_str()))
      {
        return WiFi.channel(i);
      }
    }
  }
  return 0;
}

void setup()
{
  Serial.begin(115200);
  while (!Serial);

  SPI.begin();
  rfid.PCD_Init();
  dht.begin();
  Serial.println("RFID + DHT22 Initialized. Waiting for scan...");

  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  int32_t channel = getWiFiChannel(ssid);
  WiFi.printDiag(Serial);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  WiFi.printDiag(Serial);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(espNowOnDataSent);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add peer");
    return;
  }
}

void loop()
{
  static unsigned long lastTempHumTime = 0;
  unsigned long currentMillis = millis();

  // Send temperature and humidity every 5 seconds
  if (currentMillis - lastTempHumTime >= 5000)
  {
    lastTempHumTime = currentMillis;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Prepare ESP-NOW message with only temperature and humidity
    espNow_moteData = {};
    espNow_moteData.boardId = BOARD_ID;
    espNow_moteData.readingId++;
    espNow_moteData.timeTag = currentMillis;
    espNow_moteData.temperature = temperature;
    espNow_moteData.humidity = humidity;

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&espNow_moteData, sizeof(espNow_moteData));

    if (result == ESP_OK)
    {
      Serial.println("Sent DHT22 Data (Temperature + Humidity)");
    }
    else
    {
      Serial.println("Error sending DHT22 data");
    }
  }

  // Check for RFID tags
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
  {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    char rfidTag[16] = "";
    for (byte i = 0; i < rfid.uid.size; i++)
    {
      char hexStr[4];
      snprintf(hexStr, sizeof(hexStr), "%02X", rfid.uid.uidByte[i]);
      strcat(rfidTag, hexStr);
    }

    Serial.print("RFID Tag UID: ");
    Serial.println(rfidTag);

    // Get tag name
    const char *tagName = getTagName(rfidTag);

    // Prepare ESP-NOW message with RFID data and DHT22 readings
    espNow_moteData = {};
    espNow_moteData.boardId = BOARD_ID;
    espNow_moteData.readingId++;
    espNow_moteData.timeTag = currentMillis;
    strcpy(espNow_moteData.rfidTag, rfidTag);
    strcpy(espNow_moteData.tagName, tagName);
    espNow_moteData.temperature = temperature;
    espNow_moteData.humidity = humidity;

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&espNow_moteData, sizeof(espNow_moteData));

    if (result == ESP_OK)
    {
      Serial.println("Sent RFID Data (UID + Name + Temp + Humidity)");
    }
    else
    {
      Serial.println("Error sending RFID data");
    }

    rfid.PICC_HaltA();
  }
}