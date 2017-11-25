#include <Arduino.h>

#define FASTLED_ALLOW_INTERRUPTS 0
#include "FastLED.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

//#include <ArduinoJson.h>
#include <PubSubClient.h>

FASTLED_USING_NAMESPACE

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define DATA_PIN1 D1
#define DATA_PIN2 D2
#define DATA_PIN3 D3
#define DATA_PIN4 D4
#define DATA_PIN5 D5
#define DATA_PIN6 D6
#define DATA_PIN7 D7
#define DATA_PIN8 D8

#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS 22
#define ARM_LEN 8
#define EDGE_LEN 14
#define EDGE_HALF 7

#define FADE_AMOUNT 10
#define FADE_DELAY 15

CRGB leds[NUM_LEDS];

#define FRAMES_PER_SECOND 60

const char *WIFI_SSID = "TP-LINK_2.4";
const char *WIFI_PASSWORD = "1111111111111";

// MQTT: ID, server IP, port, username and password
const PROGMEM char *MQTT_CLIENT_ID = "esp8266-3922b6";
IPAddress MQTT_SERVER_IP = IPAddress(192, 168, 1, 114);
uint16_t MQTT_SERVER_PORT = 1883;
const PROGMEM char *MQTT_USER = "Tal_LEDLamp";
const PROGMEM char *MQTT_PASSWORD = "1qazxsw2";

// MQTT: topics
// state
const PROGMEM char *MQTT_LIGHT_STATE_TOPIC = "home/esp8266_lamp/light/status";
const PROGMEM char *MQTT_LIGHT_COMMAND_TOPIC = "home/esp8266_lamp/light/switch";

// brightness
const PROGMEM char *MQTT_LIGHT_BRIGHTNESS_STATE_TOPIC = "home/esp8266_lamp/brightness/status";
const PROGMEM char *MQTT_LIGHT_BRIGHTNESS_COMMAND_TOPIC = "home/esp8266_lamp/brightness/set";

// colors (rgb)
const PROGMEM char *MQTT_LIGHT_RGB_STATE_TOPIC = "home/esp8266_lamp/rgb/status";
const PROGMEM char *MQTT_LIGHT_RGB_COMMAND_TOPIC = "home/esp8266_lamp/rgb/set";

// effects
const PROGMEM char *MQTT_LIGHT_EFFECT_STATE_TOPIC = "home/esp8266_lamp/effect/status";
const PROGMEM char *MQTT_LIGHT_EFFECT_COMMAND_TOPIC = "home/esp8266_lamp/effect/set";

// payloads by default (on/off)
const PROGMEM char *LIGHT_ON = "ON";
const PROGMEM char *LIGHT_OFF = "OFF";

//const int BUFFER_SIZE = JSON_OBJECT_SIZE(15);

// Maintained state for reporting to HA
boolean m_rgb_state = false;
uint8_t m_rgb_brightness = 100;
uint8_t m_rgb_red = 255;
uint8_t m_rgb_green = 255;
uint8_t m_rgb_blue = 255;

// buffer used to send/receive data with MQTT
const uint8_t MSG_BUFFER_SIZE = 20;
char m_msg_buffer[MSG_BUFFER_SIZE];

//Gradually fades to newColor from initial state, starting from the center beams, spreading out.
void blendToColor(CRGB newColor, unsigned char fadeAmount, unsigned int repeatAmount)
{
    Serial.print("INFO:\tSetting LEDs: ");
    Serial.print(newColor.r);
    Serial.print(", ");
    Serial.print(newColor.g);
    Serial.print(", ");
    Serial.println(newColor.b);

    unsigned char fadeArray[NUM_LEDS] = {0};
    CRGB ledsOrig[NUM_LEDS];
    //Remember initial color.
    for (unsigned int i = 0; i < NUM_LEDS; i++)
    {
        ledsOrig[i] = leds[i];
    }

    for (unsigned int i = 0; i < ARM_LEN + EDGE_HALF; i++)
    {
        for (unsigned int ii = 0; ii < repeatAmount; ii++) //to allow for smooth change, fade more times but by smaller increments.
        {
            for (unsigned int j = 0; j <= i; j++)
            {
                leds[j] = blend(ledsOrig[j], newColor, fadeArray[j]);

                fadeArray[j] = fadeArray[j] + fadeAmount;
                if (fadeArray[j] < fadeAmount)
                    fadeArray[j] = 255; //handle overflow
                if (j >= ARM_LEN)       //if started flling in main edge, fill it in from both sides.
                {
                    unsigned int index = (NUM_LEDS - (j - ARM_LEN)) - 1;
                    leds[index] = blend(ledsOrig[index], newColor, fadeArray[index]);
                    fadeArray[index] = fadeArray[index] + fadeAmount;
                    if (fadeArray[index] < fadeAmount)
                        fadeArray[index] = 255; //handle overflow
                }
            }
            FastLED.show();
            FastLED.delay(FADE_DELAY);
        }
    }
    FastLED.delay(25);
    for (unsigned int i = 0; i < 255 / fadeAmount; i++)
    {
        for (unsigned int j = 0; j < NUM_LEDS; j++)
        {
            leds[j] = blend(ledsOrig[j], newColor, fadeArray[j]);
            fadeArray[j] = fadeArray[j] + fadeAmount;
            if (fadeArray[j] < fadeAmount)
                fadeArray[j] = 255;
        }
        FastLED.show();
        FastLED.delay(FADE_DELAY + 5);
    }
    fill_solid(leds, NUM_LEDS, newColor);
    FastLED.show();
}

//Default fadeAmount and repeatAmount setting.
void blendToColor(CRGB newColor)
{
    blendToColor(newColor, FADE_AMOUNT, 3);
}

//Autofills repeatAmount
void blendToColor(CRGB newColor, unsigned char fadeAmount)
{
    blendToColor(newColor, fadeAmount, 30 / fadeAmount);
}

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// function called to adapt the brightness and the color of the led
void setColor(uint8_t p_red, uint8_t p_green, uint8_t p_blue)
{
    uint8_t brightness = map(m_rgb_brightness, 0, 100, 0, 255);
    CRGB color = CRGB(p_red, p_green, p_blue);
    color.fadeLightBy(255 - brightness);
    blendToColor(color,6,6);
}

// function called to publish the state of the led (on/off)
void publishRGBState()
{
    if (m_rgb_state)
    {
        client.publish(MQTT_LIGHT_STATE_TOPIC, LIGHT_ON, true);
    }
    else
    {
        client.publish(MQTT_LIGHT_STATE_TOPIC, LIGHT_OFF, true);
    }
}

// function called to publish the brightness of the led (0-100)
void publishRGBBrightness()
{
    snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d", m_rgb_brightness);
    client.publish(MQTT_LIGHT_BRIGHTNESS_STATE_TOPIC, m_msg_buffer, true);
}

// function called to publish the colors of the led (xx(x),xx(x),xx(x))
void publishRGBColor()
{
    snprintf(m_msg_buffer, MSG_BUFFER_SIZE, "%d,%d,%d", m_rgb_red, m_rgb_green, m_rgb_blue);
    client.publish(MQTT_LIGHT_RGB_STATE_TOPIC, m_msg_buffer, true);
}

// function called when a MQTT message arrived
void callback(char *p_topic, byte *p_payload, unsigned int p_length)
{
    // concat the payload into a string
    String payload;
    for (uint8_t i = 0; i < p_length; i++)
    {
        payload.concat((char)p_payload[i]);
    }
    Serial.print("DEBUG:\tReceived message from \"");
    Serial.print(p_topic);
    Serial.print("\"\n\tPayload: \"");
    Serial.print(payload);
    Serial.println("\"");
    // handle message topic
    if (String(MQTT_LIGHT_COMMAND_TOPIC).equals(p_topic))
    {
        // test if the payload is equal to "ON" or "OFF"
        if (payload.equals(String(LIGHT_ON)))
        {
            if (!m_rgb_state)
            {
                m_rgb_state = true;
                setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
                publishRGBState();
            }
        }
        else if (payload.equals(String(LIGHT_OFF)))
        {
            if (m_rgb_state)
            {
                m_rgb_state = false;
                setColor(0, 0, 0);
                publishRGBState();
            }
        }
    }
    else if (String(MQTT_LIGHT_BRIGHTNESS_COMMAND_TOPIC).equals(p_topic))
    {
        uint8_t brightness = payload.toInt();
        if (brightness < 0 || brightness > 100)
        {
            // do nothing...
            return;
        }
        else
        {
            m_rgb_brightness = brightness;
            setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
            publishRGBBrightness();
        }
    }
    else if (String(MQTT_LIGHT_RGB_COMMAND_TOPIC).equals(p_topic))
    {
        // get the position of the first and second commas
        uint8_t firstIndex = payload.indexOf(',');
        uint8_t lastIndex = payload.lastIndexOf(',');

        uint8_t rgb_red = payload.substring(0, firstIndex).toInt();
        if (rgb_red < 0 || rgb_red > 255)
        {
            return;
        }
        else
        {
            m_rgb_red = rgb_red;
        }

        uint8_t rgb_green = payload.substring(firstIndex + 1, lastIndex).toInt();
        if (rgb_green < 0 || rgb_green > 255)
        {
            return;
        }
        else
        {
            m_rgb_green = rgb_green;
        }

        uint8_t rgb_blue = payload.substring(lastIndex + 1).toInt();
        if (rgb_blue < 0 || rgb_blue > 255)
        {
            return;
        }
        else
        {
            m_rgb_blue = rgb_blue;
        }

        setColor(m_rgb_red, m_rgb_green, m_rgb_blue);
        publishRGBColor();
    }
}

void ICACHE_FLASH_ATTR SetupFastLED()
{
    // Tell FastLED about the LED strip configuration, all 6 strips are identical.
    //FastLED.addLeds<LED_TYPE, DATA_PIN1, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN2, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN3, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN4, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN5, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.addLeds<LED_TYPE, DATA_PIN6, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN7, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN8, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setCorrection(0xFFB978);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    blendToColor(CRGB::White, 6);
}

void ICACHE_FLASH_ATTR setup()
{
    delay(3000); // 3 second delay for recovery

    Serial.begin(921600);

    Serial.println("\n\n\n\nINFO:\tBooting...");

    //Init lamp to White
    SetupFastLED();

    //Init WiFi connection
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.print("\nINFO:\tConnecting to ");
        Serial.println(WIFI_SSID);

        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        while (WiFi.status() != WL_CONNECTED)
        {
            FastLED.delay(500);
            Serial.print(".");
        }
    }

    Serial.println("\nINFO:\tWiFi connected");
    Serial.print("INFO:\tIP address: ");
    Serial.println(WiFi.localIP());

    Serial.println("\nINFO:\tSetting up mDNS");
    if (!MDNS.begin("esp8266-3922b6"))
    {
        Serial.println("ERROR:\tfailed, Rebooting...");
        FastLED.delay(5000);
        ESP.restart();
    }
    else
    {
        Serial.println("INFO:\tmDNS ready, device name: \"esp8266-3922b6\"");
    }


    Serial.println("\nINFO:\tSearching for MQTT Broker");
    int n = MDNS.queryService("ssh", "tcp");
    if (n == 0)
    {
        Serial.print("INFO:\tNo broker found!");
    }
    else
    {
        Serial.print("INFO:\tFound ");
        Serial.print(n);
        Serial.print(" brokers:");
        // at least one MQTT service is found
        // ip no and port of the first one is MDNS.IP(0) and MDNS.port(0)
        for (int i = 0; i < n; i++)
        {
            Serial.print("\n\t  -- ");
            Serial.print(MDNS.hostname(i));
            if (MDNS.hostname(i).equalsIgnoreCase("hassio"))
            {
                Serial.print("\n\t     - Home Assistant!");
                MQTT_SERVER_IP = MDNS.IP(i);
            }
        }
    }

    Serial.print("\n\nINFO:\tWill connect to ");
    Serial.print(MQTT_SERVER_IP);
    Serial.print(":");
    Serial.println(MQTT_SERVER_PORT);

    client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);

    client.setCallback(callback);

    ArduinoOTA.onStart([]() {
        Serial.println("\nINFO:\tOTA Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nINFO:\tOTA End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("INFO:\tOTA Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("ERROR:\tOTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
            Serial.println("End Failed");
    });

    ArduinoOTA.begin();
}

void ICACHE_FLASH_ATTR reconnect()
{
    // Loop until we're reconnected
    for (int i=0; i<5 && !client.connected(); i++)
    {
        Serial.println("INFO:\tAttempting MQTT connection");
        // Attempt to connect
        if (client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD))
        {
            Serial.println("INFO:\tConnected");

            // Once connected, publish an announcement...
            // publish the initial values
            //publishRGBState();
            //publishRGBBrightness();
            //publishRGBColor();

            // ... and resubscribe
            client.subscribe(MQTT_LIGHT_COMMAND_TOPIC);
            client.subscribe(MQTT_LIGHT_BRIGHTNESS_COMMAND_TOPIC);
            client.subscribe(MQTT_LIGHT_RGB_COMMAND_TOPIC);
            client.subscribe(MQTT_LIGHT_EFFECT_COMMAND_TOPIC);
        }
        else
        {
            Serial.print("ERROR:\tfailed, rc=");
            Serial.print(client.state());
            Serial.print("DEBUG:\ttry again in 5 seconds");
            // Wait 5 seconds before retrying
            for (int j = 0; j < 5; j++)
            {
                FastLED.delay(1000);
                Serial.print(".");
            }
            Serial.println();
        }
    }
}

void loop()
{
    ArduinoOTA.handle();
    // EVERY_N_MILLISECONDS(1000 / FRAMES_PER_SECOND)
    // {
    //     FastLED.show();
    // }
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();
}