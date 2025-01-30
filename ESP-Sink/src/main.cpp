#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi and MQTT Configuration
const char *ssid = "iot";
const char *password = "iotisis;";
const char *mqtt_server = "192.168.3.9";
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;

// Variables for received MQTT values
float receivedTemp = 0.0;
bool receivedAlert = false;
bool stockEmpty = false;  // Stock status variable

// MAC Address of the Mote (Receiver)
uint8_t moteMAC[] = {0xEC, 0x62, 0x60, 0x5A, 0x7E, 0x78};
esp_now_peer_info_t peerInfo;

// Structure for Receiving ESP-NOW Data (Mote → Sink)
typedef struct struct_mote2sinkMessage
{
  int boardId;
  int readingId;
  int timeTag;
  char rfidTag[16];
  char tagName[32];
  float temperature;
  float humidity;
} struct_mote2sinkMessage;

struct_mote2sinkMessage espNow_incomingReadings;

// Structure for Sending ESP-NOW Data (Sink → Mote)
typedef struct struct_sink2moteMessage
{
  float temperature;
  bool alert;
  bool stockEmpty;  // Added Stock Status to be sent to the Mote
} struct_sink2moteMessage;

struct_sink2moteMessage espNow_outgoingMessage;

// ESP-NOW Receive Callback (Mote → Sink)
void espNowOnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
{
  memcpy(&espNow_incomingReadings, incomingData, sizeof(espNow_incomingReadings));

  Serial.println("\n[ESP-NOW] Data received from mote:");
  Serial.print("Board ID: "); Serial.println(espNow_incomingReadings.boardId);
  Serial.print("RFID Tag: "); Serial.println(espNow_incomingReadings.rfidTag);
  Serial.print("Tag Name: "); Serial.println(espNow_incomingReadings.tagName);
  Serial.print("Temperature: "); Serial.println(espNow_incomingReadings.temperature);
  Serial.print("Humidity: "); Serial.println(espNow_incomingReadings.humidity);

  // Publish received data to MQTT
  client.publish("esp32/rfid/tag", espNow_incomingReadings.rfidTag);
  client.publish("esp32/rfid/name", espNow_incomingReadings.tagName);

  char tempStr[8], humStr[8];
  dtostrf(espNow_incomingReadings.temperature, 6, 2, tempStr);
  dtostrf(espNow_incomingReadings.humidity, 6, 2, humStr);

  client.publish("esp32/dht22/temperature", tempStr);
  client.publish("esp32/dht22/humidity", humStr);
}

// ESP-NOW Send Callback (Sink → Mote)
void espNowOnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("[ESP-NOW] Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

// MQTT Callback Function - Handles received MQTT messages
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String message = "";
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  Serial.print("\n[MQTT] Message received on topic: ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Check if the message is from node-red/temp
  if (strcmp(topic, "node-red/temp") == 0)
  {
    receivedTemp = message.toFloat();
    Serial.print("Updated received temperature: ");
    Serial.println(receivedTemp);
  }

  // Check if the message is from node-red/alert
  else if (strcmp(topic, "node-red/alert") == 0)
  {
    receivedAlert = (message == "true");
    Serial.print("Updated received alert: ");
    Serial.println(receivedAlert ? "TRUE (High Temp)" : "FALSE (Normal)");
  }

  // Check if the message is from node-red/stock
  else if (strcmp(topic, "node-red/stock") == 0)
  {
    stockEmpty = (message == "true");
    Serial.print("Updated Stock Status: ");
    Serial.println(stockEmpty ? "STOCK EMPTY ⚠️" : "STOCK OK ✅");
  }
}

// MQTT Reconnection Function
void mqttReconnect()
{
  while (!client.connected())
  {
    Serial.print("[MQTT] Attempting connection...");
    if (client.connect("ESP32Client"))
    {
      Serial.println(" Connected!");
      client.subscribe("node-red/temp");
      client.subscribe("node-red/alert");
      client.subscribe("node-red/stock");  // Subscribe to stock topic
    }
    else
    {
      Serial.print(" Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retry in 5 seconds...");
      delay(5000);
    }
  }
}

// Send Data to Mote via ESP-NOW
void sendDataToMote()
{
  espNow_outgoingMessage.temperature = receivedTemp;
  espNow_outgoingMessage.alert = receivedAlert;
  espNow_outgoingMessage.stockEmpty = stockEmpty;  // Send Stock Status

  esp_err_t result = esp_now_send(moteMAC, (uint8_t *)&espNow_outgoingMessage, sizeof(espNow_outgoingMessage));

  if (result == ESP_OK)
  {
    Serial.print("[ESP-NOW] Sent Temp: ");
    Serial.print(receivedTemp);
    Serial.print(" | Alert: ");
    Serial.print(receivedAlert ? "TRUE (RED BLINKING)" : "FALSE (GREEN)");
    Serial.print(" | Stock: ");
    Serial.println(stockEmpty ? "STOCK EMPTY ⚠️" : "STOCK OK ✅");
  }
  else
  {
    Serial.println("[ESP-NOW] Sending Failed");
  }
}

void setup()
{
  Serial.begin(115200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected!");

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("[ESP-NOW] Error initializing");
    return;
  }
  esp_now_register_recv_cb(espNowOnDataRecv);
  esp_now_register_send_cb(espNowOnDataSent);

  // Register the Mote as a Peer
  memcpy(peerInfo.peer_addr, moteMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("[ESP-NOW] Failed to add mote as peer");
    return;
  }

  // Initialize MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
}

void loop()
{
  if (!client.connected())
  {
    mqttReconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 5000) // Send Data Every 5 Seconds
  {
    lastMsg = now;
    sendDataToMote();
  }
}
