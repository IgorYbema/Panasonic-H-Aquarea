#if defined(ESP8266)
#define NUMGPIO 3
#include <ESP8266WiFi.h>
#include <ESP8266WiFiGratuitous.h>
#elif defined(ESP32)
#define NUMGPIO 5
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#endif

struct gpioSettingsStruct {
#if defined(ESP8266)
  unsigned int gpioPin[NUMGPIO] = {1, 3, 16};
  unsigned int gpioMode[NUMGPIO] = {INPUT_PULLUP, INPUT_PULLUP, INPUT_PULLUP};
#elif defined(ESP32)
  unsigned int gpioPin[NUMGPIO] = {33, 34, 35, 36, 37};
  unsigned int gpioMode[NUMGPIO] = {INPUT_PULLUP, INPUT_PULLUP, INPUT_PULLUP};
#endif
};

void setupGPIO(gpioSettingsStruct gpioSettings);
