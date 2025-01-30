#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_now.h>
#include <WiFi.h>

// OLED Display Configuration
#define OLED_SDA 26
#define OLED_SCL 25
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// RGB LED Configuration (Use PWM Channels)
#define RED_PIN 23
#define GREEN_PIN 21
#define BLUE_PIN 22

// PWM Channels for LED
#define PWM_RED 0
#define PWM_GREEN 1
#define PWM_BLUE 2

// Function Prototype
void setColor(int red, int green, int blue);

// Variables for Received Data
volatile float receivedTemp = 0.0;
volatile bool receivedAlert = false;
volatile bool receivedStockEmpty = false;  // Added Stock Status Variable

// Structure for Receiving ESP-NOW Data
typedef struct struct_sink2moteMessage
{
    float temperature;
    bool alert;
    bool stockEmpty;
} struct_sink2moteMessage;

struct_sink2moteMessage espNow_incomingData;

// ESP-NOW Receive Callback
void espNowOnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
{
    memcpy(&espNow_incomingData, incomingData, sizeof(espNow_incomingData));

    receivedTemp = espNow_incomingData.temperature;
    receivedAlert = espNow_incomingData.alert;
    receivedStockEmpty = espNow_incomingData.stockEmpty;  // Receive Stock Status

    Serial.print("Received Temperature: ");
    Serial.println(receivedTemp);
    Serial.print("Received Alert: ");
    Serial.println(receivedAlert ? "TRUE (RED BLINKING)" : "FALSE (GREEN)");
    Serial.print("Received Stock Status: ");
    Serial.println(receivedStockEmpty ? "STOCK EMPTY (YELLOW BLINKING)" : "STOCK OK");

    // Update OLED Display
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.print("Temp: ");
    display.print(receivedTemp);
    display.print("C");
    display.display();
}

void setup()
{
    Serial.begin(115200);

    // Initialize I2C for OLED
    Wire.begin(OLED_SDA, OLED_SCL);

    // Initialize OLED Display
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        Serial.println("SSD1306 allocation failed");
        for (;;)
            ;
    }
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.print("Waiting...");
    display.display();

    // Initialize RGB LED Pins as PWM Outputs
    ledcSetup(PWM_RED, 5000, 8);
    ledcSetup(PWM_GREEN, 5000, 8);
    ledcSetup(PWM_BLUE, 5000, 8);

    ledcAttachPin(RED_PIN, PWM_RED);
    ledcAttachPin(GREEN_PIN, PWM_GREEN);
    ledcAttachPin(BLUE_PIN, PWM_BLUE);

    // Initialize ESP-NOW
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_recv_cb(espNowOnDataRecv);
}

void loop()
{
    if (receivedStockEmpty && receivedAlert)
    {
        // Alternate Red and Yellow Blinking when both are TRUE
        setColor(255, 0, 0); // Red ON
        delay(500);
        setColor(0, 0, 0); // LED OFF
        delay(500);
        setColor(0, 0, 255); // Yellow ON
        delay(500);
        setColor(0, 0, 0); // LED OFF
        delay(500);
    }
    else if (receivedStockEmpty)
    {
        // If Stock is Empty → Yellow Blinking
        setColor(0, 0, 255); // Yellow ON
        delay(500);
        setColor(0, 0, 0); // LED OFF
        delay(500);
    }
    else if (receivedAlert)
    {
        // If Temperature Alert → Red Blinking
        setColor(255, 0, 0); // Red ON
        delay(500);
        setColor(0, 0, 0); // LED OFF
        delay(500);
    }
    else
    {
        // Normal Condition → Solid Green
        setColor(0, 255, 0); // Green ON
    }
}


// Function to Set RGB LED Color (Using PWM)
void setColor(int red, int green, int blue)
{
    ledcWrite(PWM_RED, red);
    ledcWrite(PWM_GREEN, green);
    ledcWrite(PWM_BLUE, blue);
}
