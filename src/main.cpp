#include <Arduino.h>

#include "FastLED.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <ArduinoJson.h>
#include <PubSubClient.h>

FASTLED_USING_NAMESPACE

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

//Wifi data.
#define CONFIG_WIFI_SSID "TP-LINK_2.4"
#define CONFIG_WIFI_PASS "1111111111111"

// MQTT
#define CONFIG_MQTT_HOST "Pi3_HomeAssistant"
#define CONFIG_MQTT_USER "Tal_LEDLamp"
#define CONFIG_MQTT_PASS "1qazxsw2"

#define CONFIG_MQTT_CLIENT_ID "ESP8266_Tal_Lamp" // Must be unique on the MQTT network

// MQTT Topics
#define CONFIG_MQTT_TOPIC_STATE "home/esp8266_lamp"
#define CONFIG_MQTT_TOPIC_SET "home/esp8266_lamp/set"

#define CONFIG_MQTT_PAYLOAD_ON "ON"
#define CONFIG_MQTT_PAYLOAD_OFF "OFF"

// Miscellaneous
// Default number of flashes if no value was given
#define CONFIG_DEFAULT_FLASH_LENGTH 2
// Number of seconds for one transition in colorfade mode
#define CONFIG_COLORFADE_TIME_SLOW 10
#define CONFIG_COLORFADE_TIME_FAST 3

#define DATA_PIN1 D1
#define DATA_PIN2 D2
#define DATA_PIN3 D3
#define DATA_PIN4 D4
#define DATA_PIN5 D5
#define DATA_PIN6 D6
#define DATA_PIN7 D7

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

//Gradually fades to newColor from initial state, starting from the center beams, spreading out.
void ICACHE_FLASH_ATTR blendToColor(CRGB newColor, unsigned char fadeAmount, unsigned int repeatAmount)
{
    Serial.println("Setting LEDs:");
    Serial.print("r: ");
    Serial.print(newColor.r);
    Serial.print(", g: ");
    Serial.print(newColor.g);
    Serial.print(", b: ");
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
            FastLED.delay(FADE_DELAY);
        }
    }
    for (unsigned int i = 0; i < 255 / fadeAmount; i++)
    {
        for (unsigned int j = 0; j < NUM_LEDS; j++)
        {
            leds[j] = blend(ledsOrig[j], newColor, fadeArray[j]);
            fadeArray[j] = fadeArray[j] + fadeAmount;
            if (fadeArray[j] < fadeAmount)
                fadeArray[j] = 255;
        }
        FastLED.delay(FADE_DELAY);
    }
    fill_solid(leds, NUM_LEDS, newColor);
}

//Default fadeAmount and repeatAmount setting.
void ICACHE_FLASH_ATTR blendToColor(CRGB newColor)
{
    blendToColor(newColor, FADE_AMOUNT, 3);
}

//Autofills repeatAmount
void ICACHE_FLASH_ATTR blendToColor(CRGB newColor, unsigned char fadeAmount)
{
    blendToColor(newColor, fadeAmount, 30 / fadeAmount);
}

const char *ssid = CONFIG_WIFI_SSID;
const char *pass = CONFIG_WIFI_PASS;

const char *mqtt_server = CONFIG_MQTT_HOST;
const char *mqtt_username = CONFIG_MQTT_USER;
const char *mqtt_password = CONFIG_MQTT_PASS;
const char *client_id = CONFIG_MQTT_CLIENT_ID;

// Topics
const char *light_state_topic = CONFIG_MQTT_TOPIC_STATE;
const char *light_set_topic = CONFIG_MQTT_TOPIC_SET;

const char *on_cmd = CONFIG_MQTT_PAYLOAD_ON;
const char *off_cmd = CONFIG_MQTT_PAYLOAD_OFF;

const int BUFFER_SIZE = JSON_OBJECT_SIZE(15);

// Maintained state for reporting to HA
byte red = 255;
byte green = 255;
byte blue = 255;
byte brightness = 255;

// Real values to write to the LEDs (ex. including brightness and state)
byte realRed = 0;
byte realGreen = 0;
byte realBlue = 0;

bool stateOn = false;

// Globals for fade/transitions
bool startFade = false;
int transitionTime = 0;
bool inFade = false;

// Globals for flash
bool flash = false;
bool startFlash = false;
int flashLength = 0;
unsigned long flashStartTime = 0;
byte flashRed = red;
byte flashGreen = green;
byte flashBlue = blue;
byte flashBrightness = brightness;

// Globals for colorfade
bool colorfade = false;
int currentColor = 0;

WiFiClient espClient;
PubSubClient client(espClient);

bool ICACHE_FLASH_ATTR processJson(char *message)
{
    StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

    JsonObject &root = jsonBuffer.parseObject(message);

    if (!root.success())
    {
        Serial.println("parseObject() failed");
        return false;
    }

    if (root.containsKey("state"))
    {
        if (strcmp(root["state"], on_cmd) == 0)
        {
            stateOn = true;
        }
        else if (strcmp(root["state"], off_cmd) == 0)
        {
            stateOn = false;
        }
    }

    // If "flash" is included, treat RGB and brightness differently
    if (root.containsKey("flash") ||
        (root.containsKey("effect") && strcmp(root["effect"], "flash") == 0))
    {

        if (root.containsKey("flash"))
        {
            flashLength = (int)root["flash"] * 1000;
        }
        else
        {
            flashLength = CONFIG_DEFAULT_FLASH_LENGTH * 1000;
        }

        if (root.containsKey("brightness"))
        {
            flashBrightness = root["brightness"];
        }
        else
        {
            flashBrightness = brightness;
        }

        if (root.containsKey("color"))
        {
            flashRed = root["color"]["r"];
            flashGreen = root["color"]["g"];
            flashBlue = root["color"]["b"];
        }
        else
        {
            flashRed = red;
            flashGreen = green;
            flashBlue = blue;
        }

        flash = true;
        startFlash = true;
    }
    else if (root.containsKey("effect") &&
             (strcmp(root["effect"], "colorfade_slow") == 0 || strcmp(root["effect"], "colorfade_fast") == 0))
    {
        flash = false;
        colorfade = true;
        currentColor = 0;
        if (strcmp(root["effect"], "colorfade_slow") == 0)
        {
            transitionTime = CONFIG_COLORFADE_TIME_SLOW;
        }
        else
        {
            transitionTime = CONFIG_COLORFADE_TIME_FAST;
        }
    }
    else if (colorfade && !root.containsKey("color") && root.containsKey("brightness"))
    {
        // Adjust brightness during colorfade
        // (will be applied when fading to the next color)
        brightness = root["brightness"];
    }
    else
    { // No effect
        flash = false;
        colorfade = false;

        if (root.containsKey("color"))
        {
            red = root["color"]["r"];
            green = root["color"]["g"];
            blue = root["color"]["b"];
        }

        if (root.containsKey("brightness"))
        {
            brightness = root["brightness"];
        }

        if (root.containsKey("transition"))
        {
            transitionTime = root["transition"];
        }
        else
        {
            transitionTime = 0;
        }
    }

    return true;
}

void ICACHE_FLASH_ATTR sendState()
{
    StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

    JsonObject &root = jsonBuffer.createObject();

    root["state"] = (stateOn) ? on_cmd : off_cmd;
    JsonObject &color = root.createNestedObject("color");
    color["r"] = red;
    color["g"] = green;
    color["b"] = blue;

    root["brightness"] = brightness;

    if (colorfade)
    {
         if (transitionTime == CONFIG_COLORFADE_TIME_SLOW)
         {
             root["effect"] = "colorfade_slow";
         }
         else
         {
             root["effect"] = "colorfade_fast";
         }
     }
     else
     {
         root["effect"] = "null";
     }

    char buffer[root.measureLength() + 1];
    root.printTo(buffer, sizeof(buffer));

    client.publish(light_state_topic, buffer, true);
}

void ICACHE_FLASH_ATTR callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    char message[length + 1];
    for (int i = 0; i < length; i++)
    {
        message[i] = (char)payload[i];
    }
    message[length] = '\0';
    Serial.println(message);

    if (!processJson(message))
    {
        return;
    }

    if (stateOn)
    {
        // Update lights
        realRed = red;
        realGreen = green;
        realBlue = blue;
    }
    else
    {
        realRed = 0;
        realGreen = 0;
        realBlue = 0;
    }

    startFade = true;
    inFade = false; // Kill the current fade

    sendState();
}

void ICACHE_FLASH_ATTR SetupFastLED()
{
    // Tell FastLED about the LED strip configuration, all 6 strips are identical.
    FastLED.addLeds<LED_TYPE, DATA_PIN1, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN2, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN3, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN4, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN5, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN6, COLOR_ORDER>(leds, NUM_LEDS);
    //FastLED.addLeds<LED_TYPE, DATA_PIN7, COLOR_ORDER>(leds, NUM_LEDS);

    // set master brightness control
    FastLED.setBrightness(brightness);
    blendToColor(CRGB::Black);
}

void ICACHE_FLASH_ATTR setup()
{
    delay(3000); // 3 second delay for recovery

    Serial.begin(9600);
    Serial.println("Booting");

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Trying to connect to network...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        delay(5000);
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("Connection Failed! Rebooting...");
            Serial.println(WiFi.status());
            delay(5000);
            ESP.restart();
        }
    }

    Serial.println("Connected to network.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);


    ArduinoOTA.onStart([]() {
        Serial.println("OTA Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
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

    SetupFastLED();
    Serial.println("Ready");
}

void ICACHE_FLASH_ATTR reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect(client_id, mqtt_username, mqtt_password))
        {
            Serial.println("connected");
            client.subscribe(light_set_topic);
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void ICACHE_FLASH_ATTR setColor(int inR, int inG, int inB)
{
    //fill_solid(leds,NUM_LEDS,CRGB(inR,inG,inB));
    blendToColor(CRGB(inR, inG, inB));
}

void ICACHE_FLASH_ATTR loop()
{
    ArduinoOTA.handle();
    EVERY_N_MILLISECONDS(1000 / FRAMES_PER_SECOND)
    {
        FastLED.show();
    }
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();
    if (flash)
    {
        if (startFlash)
        {
            startFlash = false;
            flashStartTime = millis();
        }

        if ((millis() - flashStartTime) <= flashLength)
        {
            if ((millis() - flashStartTime) % 1000 <= 500)
            {
                blendToColor(CRGB(flashRed, flashGreen, flashBlue));
            }
            else
            {
                blendToColor(CRGB::Black);
                // If you'd prefer the flashing to happen "on top of"
                // the current color, uncomment the next line.
                // setColor(realRed, realGreen, realBlue);
            }
        }
        else
        {
            flash = false;
            blendToColor(CRGB(realRed, realGreen, realBlue));
        }
    }
}