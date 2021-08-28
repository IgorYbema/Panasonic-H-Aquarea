#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFiGratuitous.h>
#include <PubSubClient.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "heishamon.h"

static IPAddress apIP(192, 168, 4, 1);

int getFreeMemory(void);
String getUptime(void);
void setupWifi(settingsStruct *heishamonSettings);
int getWifiQuality(void);
int getFreeMemory(void);

void handleRoot(ESP8266WebServer *httpServer, float readpercentage, int mqttReconnects, settingsStruct *heishamonSettings);
void handleTableRefresh(ESP8266WebServer *httpServer, String actData[]);
void handleJsonOutput(ESP8266WebServer *httpServer, String actData[]);
void handleFactoryReset(ESP8266WebServer *httpServer);
void handleReboot(ESP8266WebServer *httpServer);
void handleDebug(ESP8266WebServer *httpServer, char *hex, byte hex_len);
void settingsToJson(DynamicJsonDocument &jsonDoc, settingsStruct *heishamonSettings);
void saveJsonToConfig(DynamicJsonDocument &jsonDoc);
void loadSettings(settingsStruct *heishamonSettings);
bool handleSettings(ESP8266WebServer *httpServer, settingsStruct *heishamonSettings);
void handleWifiScan(ESP8266WebServer *httpServer);
void handleSmartcontrol(ESP8266WebServer *httpServer, settingsStruct *heishamonSettings, String actData[]);
void handleREST(ESP8266WebServer *httpServer, bool optionalPCB);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
