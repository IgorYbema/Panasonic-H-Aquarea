#if defined(ESP8266)
#define NUMGPIO 3
#include <ESP8266WiFi.h>
#include <ESP8266WiFiGratuitous.h>
#elif defined(ESP32)
#define NUMGPIO 0
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#endif

struct gpioSettingsStruct {
  unsigned int gpioPin[NUMGPIO] = {1, 3, 16};
  unsigned int gpioMode[NUMGPIO] = {INPUT_PULLUP, INPUT_PULLUP, INPUT_PULLUP};
};

void setupGPIO(gpioSettingsStruct gpioSettings);
