#include <Arduino.h>

#include "FastLED.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

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

#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS 22
#define ARM_LEN 8
#define EDGE_LEN 14
#define EDGE_HALF 7

#define FADE_AMOUNT 10
#define FADE_DELAY 15

CRGB leds[NUM_LEDS];

#define BRIGHTNESS 96
#define FRAMES_PER_SECOND 60

//Gradually fades to newColor from initial state, starting from the center beams, spreading out.
void blendToColor(CRGB newColor, unsigned char fadeAmount, unsigned int repeatAmount)
{
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
void blendToColor(CRGB newColor)
{
    blendToColor(newColor, FADE_AMOUNT, 3);
}

void blendToColor(CRGB newColor, unsigned char fadeAmount)
{
    blendToColor(newColor, fadeAmount, 30 / fadeAmount);
}

const char *ssid = "TP-LINK_2.4";
const char *password = "1111111111111";

void setup()
{
    delay(3000); // 3 second delay for recovery

    Serial.begin(9600);
    Serial.println("Booting");
    //WiFi.config(ip,gateway,subnet);
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Trying to connect to network...");
        WiFi.mode(WIFI_STA);
        WiFi.persistent(false);
        WiFi.begin(ssid, password);
        delay(5000);
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("Connection Failed! Rebooting...");
            Serial.println(WiFi.status());
            delay(5000);
            ESP.restart();
        }
    }


    // Tell FastLED about the LED strip configuration, all 6 strips are identical.
    FastLED.addLeds<LED_TYPE, DATA_PIN1, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.addLeds<LED_TYPE, DATA_PIN2, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.addLeds<LED_TYPE, DATA_PIN3, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.addLeds<LED_TYPE, DATA_PIN4, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.addLeds<LED_TYPE, DATA_PIN5, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.addLeds<LED_TYPE, DATA_PIN6, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.addLeds<LED_TYPE, DATA_PIN7, COLOR_ORDER>(leds, NUM_LEDS);

    // set master brightness control
    FastLED.setBrightness(BRIGHTNESS);

    ArduinoOTA.onStart([]() {
        Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
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

    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void loop()
{
    ArduinoOTA.handle();
    blendToColor(CHSV(random8(), 255, 255)); //Generate random color each time.
    FastLED.delay(1000);
    //FastLED.delay(1000/FRAMES_PER_SECOND);
    blendToColor(CHSV(random8(), 255, 255), 20); //Generate random color each time.
    FastLED.delay(1000);
    blendToColor(CHSV(random8(), 255, 255), 1); //Generate random color each time.
    FastLED.delay(1000);
    blendToColor(CHSV(random8(), 255, 255), 5); //Generate random color each time.
    FastLED.delay(1000);
    blendToColor(CHSV(random8(), 255, 255), 20, 6); //Generate random color each time.
    FastLED.delay(1000);
    blendToColor(CHSV(random8(), 255, 255), 10, 10); //Generate random color each time.
    FastLED.delay(1000);
    blendToColor(CRGB::Black);
    FastLED.delay(500);
    blendToColor(CRGB::White);
    FastLED.delay(500);
    EVERY_N_MILLISECONDS(1000 / FRAMES_PER_SECOND)
    {
        FastLED.show();
    }
}