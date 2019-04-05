//
// Word clock, English clockface
// 
// Fork of code from https://github.com/niekproductions/word-clock which is a fork from https://bitbucket.org/vdham/wordclock/
//

#include <FastLED.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <TimeLib.h>

// Use time or LDR light sensor for auto brightness adjustment.
//#define TIME_BRIGHTNESS
#define LDR_BRIGHTNESS

const char* OTApass     = "wordclock-OTA";

#define MIN_BRIGHTNESS  65
#define MAX_BRIGHTNESS  255
#define BRIGHTNESS      255 // legacy, keep at 255
int     lastBrightness  =  MIN_BRIGHTNESS;

#define NUM_LEDS    99
#define DATA_PIN     D3

#define PIN_SET    D5
#define PIN_UP     D6
#define PIN_DN     D7
#define LDR_PIN    A0
#define LDR_DARK        10
#define LDR_LIGHT       200

void blink();

const int   timeZone        = 1;     // Central European Time
bool        autoDST         = true;
IPAddress   timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName   = "nl.pool.ntp.org";
const int   NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte        packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP     Udp;
unsigned int localPort = 8888;

CRGB leds[NUM_LEDS];

uint8_t targetlevels[NUM_LEDS];
uint8_t currentlevels[NUM_LEDS];

bool isDST(int d, int m, int y);
bool isDSTSwitchDay(int d, int m, int y);

int current_hourword;
int next_hourword;
int currentMinutes;
int currentColor;
int phaseSetTime = 0;
int hue = 0;

CRGB rgbFront = CRGB::White;
CRGB rgbBack;
bool firstTimeBoot = true;

#define ITIS     0
#define HALF    13
#define TEN     14
#define QUARTER 15
#define TWENTY  16
#define FIVE    17
#define MINUTES 18
#define TO      19
#define PAST    20
#define OCLOCK  21


// IT IS -- HALF  -- TEN   |> (89-91) >> (92-95) >> (96-98) END.
// QUARTER   --   TWENTY   |< (88-82)       <<      (81-76) <|
// FIVE -- MINUTES -- TO   |> (63-66) >> (67-73) >> (74-75) >|
// PAST -- ONE  -- THREE   |< (62-59) << (58-56) << (55-51) <|
// TWO --  FOUR --  FIVE   |> (39-42) >> (43-46) >> (47-50) >|
// SIX -- SEVEN -- EIGHT   |< (38-36) << (35-31) << (30-26) <|
// NINE -- TEN -- ELEVEN   |> (13-16) >> (17-19) >> (20-25) >|
// TWELVE   --   O'CLOCK   |< (12-7)        <<        (6-0) <<< BEGIN

std::vector<std::vector<int>> ledsbyword = {
  {89, 90, 91},           // 0 IT IS
  {58, 57, 56},           // 1 ONE
  {39, 40, 41, 42},       // 2 TWO
  {55, 54, 53, 52, 51},   // 3 THREE
  {43, 44, 45, 46},       // 4 FOUR
  {47, 48, 49, 50},       // 5 FIVE
  {38, 37, 36},           // 6 SIX
  {35, 34, 33, 32, 31},   // 7 SEVEN
  {30, 29, 28, 27, 26},   // 8 EIGHT
  {13, 14, 15, 16},       // 9 NINE
  {17, 18, 19},           // 10 TEN
  {20, 21, 22, 23, 24, 25}, // 11 ELEVEN
  {12, 11, 10, 9, 8, 7},  // 12 TWELVE
  {92, 93, 94, 95},       // 13 HALF
  {96, 97, 98},           // 14 TEN
  {88, 87, 86, 85, 84, 83, 82}, // 15 QUARTER
  {81, 80, 79, 78, 77, 76}, // 16 TWENTY
  {63, 64, 65, 66},       // 17 FIVE
  {67, 68, 69, 70, 71, 72, 73}, // 18 MINUTES
  {74, 75},               // 19 TO
  {62, 61, 60, 59},       // 20 PAST
  {6, 5, 4, 3, 2, 1, 0}   // 21 O'CLOCK
};


void setup() {
  pinMode(LDR_PIN, INPUT);

  Serial.begin(115200);

  Serial.println("###### Boot ######");

  current_hourword = random(1, 12);
  next_hourword = current_hourword + 1;
  if (next_hourword == 13) {
    next_hourword = 1;
  }
  currentMinutes = random (0, 11); //(minute()%60)/5;

  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

  pinMode(PIN_SET, INPUT);           // set pin to input
  pinMode(PIN_UP, INPUT);           // set pin to input
  pinMode(PIN_DN, INPUT);           // set pin to input

  for (int i = 0; i < NUM_LEDS; i++) {
    targetlevels[i] = 0;
    currentlevels[i] = 0;
    leds[i] = CRGB::Black;
  }
  FastLED.show();
}

void loop() {
  int valSet;
  int valUp;
  int valDn;
  int delayKeybounce = 400;
  int hueCycleWait = 20;
  
  if (phaseSetTime == 0) {
    // calculate target brightnesses:
    int current_hourword = hour();
    if (current_hourword > 12) current_hourword = current_hourword - 12; // 12 hour clock, where 12 stays 12 and 13 becomes one
    if (current_hourword == 0) current_hourword = 12;         // 0 is also called 12
  
    int next_hourword = hour() + 1;
    if (next_hourword > 12) next_hourword = next_hourword - 12;   // 12 hour clock, where 12 stays 12 and 13 becomes one
    if (next_hourword == 0) next_hourword = 12;           // 0 is also called 12
    currentMinutes = (minute() % 60) / 5;
  }

  setTargetLevels();


  if (phaseSetTime == 0) {
    for (int i = 0; i < NUM_LEDS; ++i) {
      if (targetlevels[i] == 255) {
        leds[i] = CRGB::White; //rgbFront;
      }
      else {
        leds[i] = CRGB::Black;
      }
    }
    FastLED.show();

    valSet = digitalRead(PIN_SET);
    if (valSet == 0) { // Active low
      phaseSetTime ++;
      delay(delayKeybounce);
    }
  }
  else if (phaseSetTime == 1) {
    valUp = digitalRead(PIN_UP);
    valDn = digitalRead(PIN_DN);
    valSet = digitalRead(PIN_SET);
    if (valUp == 0) { // Active low
      currentMinutes ++; // 0 .. 11
      if (currentMinutes == 12) {
        currentMinutes = 0;
  
        current_hourword++; // 1 .. 12
        if (current_hourword == 13) {
          current_hourword = 1;
        }
  
        next_hourword++; // 1 .. 12
        if (next_hourword == 13) {
          next_hourword = 1;
        }
      }

      setTargetLevels();
      blink();
      while (true) {
        valUp = digitalRead(PIN_UP);
        if (valUp != 0) {
          break;
        }
        blink();
      }
    }
    else if (valDn == 0) { // Active low
      currentMinutes --; // 0 .. 11
      if (currentMinutes == -1) {
        currentMinutes = 11;
  
        current_hourword--; // 1 .. 12
        if (current_hourword == 0) {
          current_hourword = 12;
        }
  
        next_hourword--; // 1 .. 12
        if (next_hourword == 0) {
          next_hourword = 12;
        }
      }
  
      setTargetLevels();
      blink();
      while (true) {
        valDn = digitalRead(PIN_DN);
        if (valDn != 0) {
          break;
        }
        blink();
      }
      blink();
    }
    else if (valSet == 0) { // Active low
      if (!firstTimeBoot) {
        // Only one time the full color screen is shown to confirm the setting.
        phaseSetTime ++;
      }
      phaseSetTime ++;
      
      blink();
      while (true) {
        valSet = digitalRead(PIN_SET);
        if (valSet != 0) {
          break;
        }
        blink();
      }
    }
    else {
      blink();
    }
  }
  if (phaseSetTime == 2) { // Blink all colors on ALL leds
    firstTimeBoot = false;
    valSet = digitalRead(PIN_SET);

    uint8_t gHue = 0;
    while (gHue < 64) {
      valSet = digitalRead(PIN_SET);
      if (valSet == 0) { // Active low
        delay(delayKeybounce);
        break;
      }

      EVERY_N_MILLISECONDS(20) {
        gHue++;
      }
      fill_rainbow(leds, NUM_LEDS, gHue, 1);
      FastLED.delay(1000 / 200); // 30FPS
    }
    phaseSetTime ++;
  }
  else if (phaseSetTime == 3) {
    valUp = digitalRead(PIN_UP);
    valDn = digitalRead(PIN_DN);
    valSet = digitalRead(PIN_SET);


    if (valUp == 0) { // Active low
      currentColor += 16; // 0 .. 11
      if (currentColor > 256) {
        currentColor = 0;
      }
      delay(delayKeybounce);
    }
    else if (valDn == 0) { // Active low
      currentColor -= 16; // 0 .. 11
      if (currentColor < 0) {
        currentColor = 256;
      }
      delay(delayKeybounce);
    }
    else if (valSet == 0) { // Active low
      phaseSetTime = 0;
      delay(delayKeybounce);
    }

    if (currentColor == 0) {
      // HSV (Spectrum) to RGB color conversion
      CHSV hsv( hue, 255, 255); // pure blue in HSV Spectrum space
      hsv2rgb_spectrum( hsv, rgbBack);
      delay (hueCycleWait);
      hue++;
      if (hue > 255) {
        hue = 0;
      }
    }
    else if (currentColor == 256) {
      rgbBack = CRGB::White;
    }
    else {
      // HSV (Spectrum) to RGB color conversion
      CHSV hsv( currentColor, 255, 255); // pure blue in HSV Spectrum space
      hsv2rgb_spectrum( hsv, rgbBack);
      delay (hueCycleWait);
      hue++;
      if (hue > 255) {
        hue = 0;
      }
    }
  
    for (int i = 0; i < NUM_LEDS; ++i) {
      if (targetlevels[i] == 255) {
        leds[i] = rgbBack; //rgbFront;
      }
      else {
        leds[i] = CRGB::Black;// rgbBack; //CRGB::Black;
      }
    }
    FastLED.show();
  }
}

void setTargetLevels()
{
  for (int i = 0; i < NUM_LEDS; ++i) {
    targetlevels[i] = 0;
  }
  for (int l : ledsbyword[ITIS]) {
    // Set the word IT IS to on always.
    targetlevels[l] = 255;
  }

  switch (currentMinutes) {
    case 0:
      for (int l : ledsbyword[current_hourword]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[OCLOCK]) {
        targetlevels[l] = 255;
      }
      break;
    case 1:
      for (int l : ledsbyword[FIVE]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[MINUTES]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[PAST]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[current_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 2:
      for (int l : ledsbyword[TEN]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[MINUTES]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[PAST]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[current_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 3:
      for (int l : ledsbyword[QUARTER]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[PAST]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[current_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 4:
      for (int l : ledsbyword[TWENTY]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[MINUTES]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[PAST]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[current_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 5:
      for (int l : ledsbyword[TWENTY]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[FIVE]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[MINUTES]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[PAST]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[current_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 6:
      for (int l : ledsbyword[HALF]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[PAST]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[current_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 7:
      for (int l : ledsbyword[TWENTY]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[FIVE]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[MINUTES]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[TO]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[next_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 8:
      for (int l : ledsbyword[TWENTY]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[MINUTES]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[TO]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[next_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 9:
      for (int l : ledsbyword[QUARTER]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[TO]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[next_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 10:
      for (int l : ledsbyword[TEN]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[MINUTES]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[TO]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[next_hourword]) {
        targetlevels[l] = 255;
      }
      break;
    case 11:
      for (int l : ledsbyword[FIVE]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[MINUTES]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[TO]) {
        targetlevels[l] = 255;
      }
      for (int l : ledsbyword[next_hourword]) {
        targetlevels[l] = 255;
      }
      break;
  }
}

void blink()
{
  int loop1 = 22;
  int loop2 = 3;
  
  for (int k = 0; k < 5; k++) {
    for (int i = 0; i < NUM_LEDS; ++i) {
      if (targetlevels[i] == 255) {
        leds[i] = rgbFront;
      }
      else {
        leds[i] = CRGB::Black;
      }
    }
    FastLED.show();
    delay(loop2);

    for (int i = 0; i < NUM_LEDS; ++i) {
      leds[i] = CRGB::Black;
    }
    FastLED.show();
    delay(loop2);
  }

  for (int i = 0; i < NUM_LEDS; ++i) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();
  delay(loop1);
}
