#include "webfunctions.h"
#include "decode.h"
#include "version.h"
#include "htmlcode.h"
#include "commands.h"
#include "src/common/webserver.h"
#include "src/common/timerqueue.h"

#include "lwip/apps/sntp.h"
#include "lwip/dns.h"

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <time.h>

#define UPTIME_OVERFLOW 4294967295 // Uptime overflow value

static String wifiJsonList = "";

static uint8_t ntpservers = 0;

void log_message(char* string);
void log_message(const __FlashStringHelper *msg);

int dBmToQuality(int dBm) {
  if (dBm == 31)
    return -1;
  if (dBm <= -100)
    return 0;
  if (dBm >= -50)
    return 100;
  return 2 * (dBm + 100);
}


void getWifiScanResults(int numSsid) {
  if (numSsid > 0) { //found wifi networks
    wifiJsonList = "[";
    int indexes[numSsid];
    for (int i = 0; i < numSsid; i++) { //fill the sorted list with normal indexes first
      indexes[i] = i;
    }
    for (int i = 0; i < numSsid; i++) { //then sort
      for (int j = i + 1; j < numSsid; j++) {
        if (WiFi.RSSI(indexes[j]) > WiFi.RSSI(indexes[i])) {
          int temp = indexes[j];
          indexes[j] = indexes[i];
          indexes[i] = temp;
        }
      }
    }
    String ssid;
    for (int i = 0; i < numSsid; i++) { //then remove duplicates
      if (indexes[i] == -1) continue;
      ssid = WiFi.SSID(indexes[i]);
      for (int j = i + 1; j < numSsid; j++) {
        if (ssid == WiFi.SSID(indexes[j])) {
          indexes[j] = -1;
        }
      }
    }
    bool firstSSID = true;
    for (int i = 0; i < numSsid; i++) { //then output json
      if (indexes[i] == -1) {
        continue;
      }
      if (!firstSSID) {
        wifiJsonList = wifiJsonList + ",";
      }
      wifiJsonList = wifiJsonList + "{\"ssid\":\"" + WiFi.SSID(indexes[i]) + "\",\"rssi\":\"" + dBmToQuality(WiFi.RSSI(indexes[i])) + "%\"}";
      firstSSID = false;
    }
    wifiJsonList = wifiJsonList + "]";
  }
}

int getWifiQuality() {
  if (WiFi.status() != WL_CONNECTED)
    return -1;
  return dBmToQuality(WiFi.RSSI());
}

int getFreeMemory() {
  //store total memory at boot time
  static uint32_t total_memory = 0;
  if ( 0 == total_memory ) total_memory = ESP.getFreeHeap();

  uint32_t free_memory   = ESP.getFreeHeap();
  return (100 * free_memory / total_memory ) ; // as a %
}

// returns system uptime in seconds
char *getUptime(void) {
  static uint32_t last_uptime      = 0;
  static uint8_t  uptime_overflows = 0;

  if (millis() < last_uptime) {
    ++uptime_overflows;
  }
  last_uptime             = millis();
  uint32_t t = uptime_overflows * (UPTIME_OVERFLOW / 1000) + (last_uptime / 1000);

  uint8_t  d   = t / 86400L;
  uint8_t  h   = ((t % 86400L) / 3600L) % 60;
  uint32_t rem = t % 3600L;
  uint8_t  m   = rem / 60;
  uint8_t  sec = rem % 60;

  unsigned int len = snprintf_P(NULL, 0, PSTR("%d day%s %d hour%s %d minute%s %d second%s"), d, (d == 1) ? "" : "s", h, (h == 1) ? "" : "s", m, (m == 1) ? "" : "s", sec, (sec == 1) ? "" : "s");

  char *str = (char *)malloc(len + 2);
  if (str == NULL) {
    Serial1.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
  }

  memset(str, 0, len + 2);
  snprintf_P(str, len + 1, PSTR("%d day%s %d hour%s %d minute%s %d second%s"), d, (d == 1) ? "" : "s", h, (h == 1) ? "" : "s", m, (m == 1) ? "" : "s", sec, (sec == 1) ? "" : "s");
  return str;
}

void ntp_dns_found(const char *name, const ip4_addr *addr, void *arg) {
  sntp_stop();
  sntp_setserver(ntpservers++, addr);
  sntp_init();
}

void ntpReload(settingsStruct *heishamonSettings) {
  ip_addr_t addr;
  uint8_t len = strlen(heishamonSettings->ntp_servers);
  uint8_t ptr = 0, i = 0;
  ntpservers = 0;
  for (i = 0; i <= len; i++) {
    if (heishamonSettings->ntp_servers[i] == ',') {
      heishamonSettings->ntp_servers[i] = 0;

      uint8_t err = dns_gethostbyname(&heishamonSettings->ntp_servers[ptr], &addr, ntp_dns_found, 0);
      if (err == ERR_OK) {
        sntp_stop();
        sntp_setserver(ntpservers++, &addr);
        sntp_init();
      }
      heishamonSettings->ntp_servers[i++] = ',';
      while (heishamonSettings->ntp_servers[i] == ' ') {
        i++;
      }
      ptr = i;
    }
  }

  uint8_t err = dns_gethostbyname(&heishamonSettings->ntp_servers[ptr], &addr, ntp_dns_found, 0);
  if (err == ERR_OK) {
    sntp_stop();
    sntp_setserver(ntpservers++, &addr);
    sntp_init();
  }

  sntp_stop();
  tzStruct tz;
  memcpy_P(&tz, &tzdata[heishamonSettings->timezone], sizeof(tz));
  setTZ(tz.value);
  sntp_init();
}

void loadSettings(settingsStruct *heishamonSettings) {
  //read configuration from FS json
  log_message(F("mounting FS..."));

  if (LittleFS.begin()) {
    log_message(F("mounted file system"));
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      log_message(F("reading config file"));
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        log_message(F("opened config file"));
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument jsonDoc(1024);
        DeserializationError error = deserializeJson(jsonDoc, buf.get());
        char log_msg[512];
        serializeJson(jsonDoc, log_msg);
        log_message(log_msg);
        if (!error) {
          log_message(F("parsed json"));
          //read updated parameters, make sure no overflow
          if ( jsonDoc["wifi_ssid"] ) strncpy(heishamonSettings->wifi_ssid, jsonDoc["wifi_ssid"], sizeof(heishamonSettings->wifi_ssid));
          if ( jsonDoc["wifi_password"] ) strncpy(heishamonSettings->wifi_password, jsonDoc["wifi_password"], sizeof(heishamonSettings->wifi_password));
          if ( jsonDoc["wifi_hostname"] ) strncpy(heishamonSettings->wifi_hostname, jsonDoc["wifi_hostname"], sizeof(heishamonSettings->wifi_hostname));
          if ( jsonDoc["ota_password"] ) strncpy(heishamonSettings->ota_password, jsonDoc["ota_password"], sizeof(heishamonSettings->ota_password));
          if ( jsonDoc["mqtt_topic_base"] ) strncpy(heishamonSettings->mqtt_topic_base, jsonDoc["mqtt_topic_base"], sizeof(heishamonSettings->mqtt_topic_base));
          if ( jsonDoc["mqtt_server"] ) strncpy(heishamonSettings->mqtt_server, jsonDoc["mqtt_server"], sizeof(heishamonSettings->mqtt_server));
          if ( jsonDoc["mqtt_port"] ) strncpy(heishamonSettings->mqtt_port, jsonDoc["mqtt_port"], sizeof(heishamonSettings->mqtt_port));
          if ( jsonDoc["mqtt_username"] ) strncpy(heishamonSettings->mqtt_username, jsonDoc["mqtt_username"], sizeof(heishamonSettings->mqtt_username));
          if ( jsonDoc["mqtt_password"] ) strncpy(heishamonSettings->mqtt_password, jsonDoc["mqtt_password"], sizeof(heishamonSettings->mqtt_password));
          if ( jsonDoc["ntp_servers"] ) strncpy(heishamonSettings->ntp_servers, jsonDoc["ntp_servers"], sizeof(heishamonSettings->ntp_servers));
          if ( jsonDoc["timezone"]) heishamonSettings->timezone = jsonDoc["timezone"];
          heishamonSettings->use_1wire = ( jsonDoc["use_1wire"] == "enabled" ) ? true : false;
          heishamonSettings->use_s0 = ( jsonDoc["use_s0"] == "enabled" ) ? true : false;
          heishamonSettings->listenonly = ( jsonDoc["listenonly"] == "enabled" ) ? true : false;
          heishamonSettings->logMqtt = ( jsonDoc["logMqtt"] == "enabled" ) ? true : false;
          heishamonSettings->logHexdump = ( jsonDoc["logHexdump"] == "enabled" ) ? true : false;
          heishamonSettings->logSerial1 = ( jsonDoc["logSerial1"] == "enabled" ) ? true : false;
          heishamonSettings->optionalPCB = ( jsonDoc["optionalPCB"] == "enabled" ) ? true : false;
          if ( jsonDoc["waitTime"]) heishamonSettings->waitTime = jsonDoc["waitTime"];
          if (heishamonSettings->waitTime < 5) heishamonSettings->waitTime = 5;
          if ( jsonDoc["waitDallasTime"]) heishamonSettings->waitDallasTime = jsonDoc["waitDallasTime"];
          if (heishamonSettings->waitDallasTime < 5) heishamonSettings->waitDallasTime = 5;
          if ( jsonDoc["dallasResolution"]) heishamonSettings->dallasResolution = jsonDoc["dallasResolution"];
          if ((heishamonSettings->dallasResolution < 9) || (heishamonSettings->dallasResolution > 12) ) heishamonSettings->dallasResolution = 12;
          if ( jsonDoc["updateAllTime"]) heishamonSettings->updateAllTime = jsonDoc["updateAllTime"];
          if (heishamonSettings->updateAllTime < heishamonSettings->waitTime) heishamonSettings->updateAllTime = heishamonSettings->waitTime;
          if ( jsonDoc["updataAllDallasTime"]) heishamonSettings->updataAllDallasTime = jsonDoc["updataAllDallasTime"];
          if (heishamonSettings->updataAllDallasTime < heishamonSettings->waitDallasTime) heishamonSettings->updataAllDallasTime = heishamonSettings->waitDallasTime;
          if (jsonDoc["s0_1_gpio"]) heishamonSettings->s0Settings[0].gpiopin = jsonDoc["s0_1_gpio"];
          if (jsonDoc["s0_1_ppkwh"]) heishamonSettings->s0Settings[0].ppkwh = jsonDoc["s0_1_ppkwh"];
          if (jsonDoc["s0_1_interval"]) heishamonSettings->s0Settings[0].lowerPowerInterval = jsonDoc["s0_1_interval"];
          if (jsonDoc["s0_1_minpulsewidth"]) heishamonSettings->s0Settings[0].minimalPulseWidth = jsonDoc["s0_1_minpulsewidth"];
          if (jsonDoc["s0_1_maxpulsewidth"]) heishamonSettings->s0Settings[0].maximalPulseWidth = jsonDoc["s0_1_maxpulsewidth"];
          if (jsonDoc["s0_2_gpio"]) heishamonSettings->s0Settings[1].gpiopin = jsonDoc["s0_2_gpio"];
          if (jsonDoc["s0_2_ppkwh"]) heishamonSettings->s0Settings[1].ppkwh = jsonDoc["s0_2_ppkwh"];
          if (jsonDoc["s0_2_interval"] ) heishamonSettings->s0Settings[1].lowerPowerInterval = jsonDoc["s0_2_interval"];
          if (jsonDoc["s0_2_minpulsewidth"]) heishamonSettings->s0Settings[1].minimalPulseWidth = jsonDoc["s0_2_minpulsewidth"];
          if (jsonDoc["s0_2_maxpulsewidth"]) heishamonSettings->s0Settings[1].maximalPulseWidth = jsonDoc["s0_2_maxpulsewidth"];
          ntpReload(heishamonSettings);
        } else {
          log_message(F("Failed to load json config, forcing config reset."));
          WiFi.persistent(true);
          WiFi.disconnect();
          WiFi.persistent(false);
        }
        configFile.close();
      }
    }
    else {
      log_message(F("No config.json exists! Forcing a config reset."));
      WiFi.persistent(true);
      WiFi.disconnect();
      WiFi.persistent(false);
    }
  } else {
    log_message(F("failed to mount FS"));
  }
  //end read

}

void setupWifi(settingsStruct *heishamonSettings) {
  log_message(F("Wifi reconnecting with new configuration..."));
  //no sleep wifi
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);

  if (heishamonSettings->wifi_ssid[0] != '\0') {
    log_message(F("Wifi client mode..."));
    //WiFi.persistent(true); //breaks stuff

    if (heishamonSettings->wifi_password[0] == '\0') {
      WiFi.begin(heishamonSettings->wifi_ssid);
    } else {
      WiFi.begin(heishamonSettings->wifi_ssid, heishamonSettings->wifi_password);
    }
  }
  else {
    log_message(F("Wifi hotspot mode..."));
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(F("HeishaMon-Setup"));
  }

  if (heishamonSettings->wifi_hostname[0] == '\0') {
    //Set hostname on wifi rather than ESP_xxxxx
    WiFi.hostname(F("HeishaMon"));
  } else {
    WiFi.hostname(heishamonSettings->wifi_hostname);
  }
}

int handleFactoryReset(struct webserver_t *client) {
  switch (client->content) {
    case 0: {
        webserver_send(client, 200, (char *)"text/html", 0);
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, refreshMeta, strlen_P(refreshMeta));
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
        webserver_send_content_P(client, webBodyRebootWarning, strlen_P(webBodyRebootWarning));
      } break;
    case 1: {
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
    case 2: {
        timerqueue_insert(1, 0, -1); // Start reboot sequence
      } break;
  }

  return 0;
}

int handleReboot(struct webserver_t *client) {
  switch (client->content) {
    case 0: {
        webserver_send(client, 200, (char *)"text/html", 0);
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, refreshMeta, strlen_P(refreshMeta));
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
        webserver_send_content_P(client, webBodyRebootWarning, strlen_P(webBodyRebootWarning));
      } break;
    case 1: {
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
    case 2: {
        timerqueue_insert(5, 0, -2); // Start reboot sequence
      } break;
  }

  return 0;
}

void settingsToJson(DynamicJsonDocument &jsonDoc, settingsStruct *heishamonSettings) {
  //set jsonDoc with current settings
  jsonDoc["wifi_hostname"] = heishamonSettings->wifi_hostname;
  jsonDoc["wifi_password"] = heishamonSettings->wifi_password;
  jsonDoc["wifi_ssid"] = heishamonSettings->wifi_ssid;
  jsonDoc["ota_password"] = heishamonSettings->ota_password;
  jsonDoc["mqtt_topic_base"] = heishamonSettings->mqtt_topic_base;
  jsonDoc["mqtt_server"] = heishamonSettings->mqtt_server;
  jsonDoc["mqtt_port"] = heishamonSettings->mqtt_port;
  jsonDoc["mqtt_username"] = heishamonSettings->mqtt_username;
  jsonDoc["mqtt_password"] = heishamonSettings->mqtt_password;
  if (heishamonSettings->use_1wire) {
    jsonDoc["use_1wire"] = "enabled";
  } else {
    jsonDoc["use_1wire"] = "disabled";
  }
  if (heishamonSettings->use_s0) {
    jsonDoc["use_s0"] = "enabled";
  } else {
    jsonDoc["use_s0"] = "disabled";
  }
  if (heishamonSettings->listenonly) {
    jsonDoc["listenonly"] = "enabled";
  } else {
    jsonDoc["listenonly"] = "disabled";
  }
  if (heishamonSettings->logMqtt) {
    jsonDoc["logMqtt"] = "enabled";
  } else {
    jsonDoc["logMqtt"] = "disabled";
  }
  if (heishamonSettings->logHexdump) {
    jsonDoc["logHexdump"] = "enabled";
  } else {
    jsonDoc["logHexdump"] = "disabled";
  }
  if (heishamonSettings->logSerial1) {
    jsonDoc["logSerial1"] = "enabled";
  } else {
    jsonDoc["logSerial1"] = "disabled";
  }
  if (heishamonSettings->optionalPCB) {
    jsonDoc["optionalPCB"] = "enabled";
  } else {
    jsonDoc["optionalPCB"] = "disabled";
  }
  jsonDoc["waitTime"] = heishamonSettings->waitTime;
  jsonDoc["waitDallasTime"] = heishamonSettings->waitDallasTime;
  jsonDoc["dallasResolution"] = heishamonSettings->dallasResolution;
  jsonDoc["updateAllTime"] = heishamonSettings->updateAllTime;
  jsonDoc["updataAllDallasTime"] = heishamonSettings->updataAllDallasTime;
}

void saveJsonToConfig(DynamicJsonDocument &jsonDoc) {
  if (LittleFS.begin()) {
    File configFile = LittleFS.open("/config.json", "w");
    if (configFile) {
      serializeJson(jsonDoc, configFile);
      configFile.close();
    }
  }
}

int saveSettings(struct webserver_t *client, settingsStruct *heishamonSettings) {
  const char *wifi_ssid = NULL;
  const char *wifi_password = NULL;
  const char *new_ota_password = NULL;
  const char *current_ota_password = NULL;
  const char *use_s0 = NULL;

  bool reconnectWiFi = false;
  DynamicJsonDocument jsonDoc(1024);

  settingsToJson(jsonDoc, heishamonSettings); //stores current settings in a json document

  jsonDoc["listenonly"] = String("");
  jsonDoc["logMqtt"] = String("");
  jsonDoc["logHexdump"] = String("");
  jsonDoc["logSerial1"] = String("");
  jsonDoc["optionalPCB"] = String("");
  jsonDoc["use_1wire"] = String("");
  jsonDoc["use_s0"] = String("");

  struct websettings_t *tmp = (struct websettings_t *)client->userdata;
  while (tmp) {
    if (strcmp(tmp->name.c_str(), "wifi_hostname") == 0) {
      jsonDoc["wifi_hostname"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "mqtt_topic_base") == 0) {
      jsonDoc["mqtt_topic_base"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "mqtt_server") == 0) {
      jsonDoc["mqtt_server"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "mqtt_port") == 0) {
      jsonDoc["mqtt_port"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "mqtt_username") == 0) {
      jsonDoc["mqtt_username"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "mqtt_password") == 0) {
      jsonDoc["mqtt_password"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "use_1wire") == 0) {
      jsonDoc["use_1wire"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "use_s0") == 0) {
      jsonDoc["use_s0"] = tmp->value;
      if (strcmp(tmp->value.c_str(), "enabled") == 0) {
        use_s0 = tmp->value.c_str();
      }
    } else if (strcmp(tmp->name.c_str(), "listenonly") == 0) {
      jsonDoc["listenonly"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "logMqtt") == 0) {
      jsonDoc["logMqtt"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "logHexdump") == 0) {
      jsonDoc["logHexdump"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "logSerial1") == 0) {
      jsonDoc["logSerial1"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "optionalPCB") == 0) {
      jsonDoc["optionalPCB"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "ntp_servers") == 0) {
      jsonDoc["ntp_servers"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "timezone") == 0) {
      jsonDoc["timezone"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "waitTime") == 0) {
      jsonDoc["waitTime"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "waitDallasTime") == 0) {
      jsonDoc["waitDallasTime"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "updateAllTime") == 0) {
      jsonDoc["updateAllTime"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "dallasResolution") == 0) {
      jsonDoc["dallasResolution"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "updataAllDallasTime") == 0) {
      jsonDoc["updataAllDallasTime"] = tmp->value;
    } else if (strcmp(tmp->name.c_str(), "wifi_ssid") == 0) {
      wifi_ssid = tmp->value.c_str();
    } else if (strcmp(tmp->name.c_str(), "wifi_password") == 0) {
      wifi_password = tmp->value.c_str();
    } else if (strcmp(tmp->name.c_str(), "new_ota_password") == 0) {
      new_ota_password = tmp->value.c_str();
    } else if (strcmp(tmp->name.c_str(), "current_ota_password") == 0) {
      current_ota_password = tmp->value.c_str();
    }
    tmp = tmp->next;
  }

  tmp = (struct websettings_t *)client->userdata;
  while (tmp) {
    if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_gpio") == 0) {
      jsonDoc["s0_1_gpio"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_ppkwh") == 0) {
      jsonDoc["s0_1_ppkwh"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_interval") == 0) {
      jsonDoc["s0_1_interval"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_minpulsewidth") == 0) {
      jsonDoc["s0_1_minpulsewidth"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_maxpulsewidth") == 0) {
      jsonDoc["s0_1_maxpulsewidth"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_gpio") == 0) {
      jsonDoc["s0_2_gpio"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_ppkwh") == 0) {
      jsonDoc["s0_2_ppkwh"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_ppkwh") == 0) {
      jsonDoc["s0_2_ppkwh"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_interval") == 0) {
      jsonDoc["s0_2_interval"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_minpulsewidth") == 0) {
      jsonDoc["s0_2_minpulsewidth"] = tmp->value;
    } else if (use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_maxpulsewidth") == 0) {
      jsonDoc["s0_2_maxpulsewidth"] = tmp->value;
    }
    tmp = tmp->next;
  }

  while (client->userdata) {
    tmp = (struct websettings_t *)client->userdata;
    client->userdata = ((struct websettings_t *)(client->userdata))->next;
    free(tmp);
  }

  if (new_ota_password != NULL && strlen(new_ota_password) > 0 && current_ota_password != NULL && strlen(current_ota_password) > 0) {
    if (strcmp(heishamonSettings->ota_password, current_ota_password) == 0) {
      jsonDoc["ota_password"] = new_ota_password;
    } else {
      client->route = 111;
      return 0;
    }
  }

  if (wifi_password != NULL && wifi_ssid != NULL && strlen(wifi_ssid) > 0 && strlen(wifi_password) > 0) {
    if (strcmp(jsonDoc["wifi_ssid"], wifi_ssid) != 0 || strcmp(jsonDoc["wifi_password"], wifi_password) != 0) {
      reconnectWiFi = true;
    }
  }
  if (wifi_ssid != NULL) {
    jsonDoc["wifi_ssid"] = String(wifi_ssid);
  }
  if (wifi_password != NULL) {
    jsonDoc["wifi_password"] = String(wifi_password);
  }

  saveJsonToConfig(jsonDoc); //save to config file
  loadSettings(heishamonSettings); //load config file to current settings

  if (reconnectWiFi) {
    client->route = 112;
    return 0;
  }

  client->route = 113;
  return 0;
}

int cacheSettings(struct webserver_t *client, struct arguments_t * args) {
  struct websettings_t *tmp = (struct websettings_t *)client->userdata;
  while (tmp) {
    if (strcmp(tmp->name.c_str(), (char *)args->name) == 0) {
      char *cpy = (char *)malloc(args->len + 1);
      memset(cpy, 0, args->len + 1);
      memcpy(cpy, args->value, args->len);
      tmp->value += cpy;
      free(cpy);
      break;
    }
    tmp = tmp->next;
  }
  if (tmp == NULL) {
    websettings_t *node = new websettings_t;
    if (node == NULL) {
      Serial1.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
      ESP.restart();
      exit(-1);
    }
    node->next = NULL;
    node->name += (char *)args->name;

    if (args->value != NULL) {
      char *cpy = (char *)malloc(args->len + 1);
      if (node == NULL) {
        Serial1.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
        ESP.restart();
        exit(-1);
      }
      memset(cpy, 0, args->len + 1);
      strncpy(cpy, (char *)args->value, args->len);
      node->value += cpy;
      free(cpy);
    }

    node->next = (struct websettings_t *)client->userdata;
    client->userdata = node;
  }

  return 0;
}

int settingsNewPassword(struct webserver_t *client, settingsStruct *heishamonSettings) {
  switch (client->content) {
    case 0: {
        webserver_send(client, 200, (char *)"text/html", 0);
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
        webserver_send_content_P(client, webBodySettings1, strlen_P(webBodySettings1));
        webserver_send_content_P(client, webBodySettingsResetPasswordWarning, strlen_P(webBodySettingsResetPasswordWarning));
      } break;
    case 1: {
        webserver_send_content_P(client, refreshMeta, strlen_P(refreshMeta));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
    case 3: {
        setupConditionals();
      } break;
  }

  return 0;
}

int settingsReconnectWifi(struct webserver_t *client, settingsStruct *heishamonSettings) {
  uint16_t size = sizeof(tzdata) / sizeof(tzdata[0]);
  if (client->content == 0) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else if (client->content == 1) {
    webserver_send_content_P(client, webHeader, strlen_P(webHeader));
    webserver_send_content_P(client, webCSS, strlen_P(webCSS));
    webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
    webserver_send_content_P(client, webBodySettings1, strlen_P(webBodySettings1));
    webserver_send_content_P(client, settingsForm1, strlen_P(settingsForm1));
  } else if (client->content >= 2 && client->content < size + 2) {
    tzStruct tz;
    memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
    client->sendlist[0].size =
    snprintf_P((char *)client->sendlist[0].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
      PSTR("<option value=\"%d\">%.*s</option>"),
      client->content - 2,
      strlen(tz.name), tz.name
    );
    client->content++;

    if(client->content < size + 2) {
      memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
      client->sendlist[1].size =
      snprintf_P((char *)client->sendlist[1].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
        PSTR("<option value=\"%d\">%.*s</option>"),
        client->content - 2,
        strlen(tz.name), tz.name
      );
      client->content++;
    }


    if(client->content < size + 2) {
      memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
      client->sendlist[2].size =
      snprintf_P((char *)client->sendlist[2].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
        PSTR("<option value=\"%d\">%.*s</option>"),
        client->content - 2,
        strlen(tz.name), tz.name
      );
      client->content++;
    }


    if(client->content < size + 2) {
      memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
      client->sendlist[3].size =
      snprintf_P((char *)client->sendlist[3].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
        PSTR("<option value=\"%d\">%.*s</option>"),
        client->content - 2,
        strlen(tz.name), tz.name
      );
      client->content++;
    }

    if(client->content < size + 2) {
      memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
      client->sendlist[4].size =
      snprintf_P((char *)client->sendlist[4].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
        PSTR("<option value=\"%d\">%.*s</option>"),
        client->content - 2,
        strlen(tz.name), tz.name
      );
      client->content++;
    }
    client->content--;
  } else if (client->content == size + 2) {
    webserver_send_content_P(client, settingsForm2, strlen_P(settingsForm2));
    webserver_send_content_P(client, menuJS, strlen_P(menuJS));
  } else if (client->content == size + 3) {
    webserver_send_content_P(client, webBodySettingsNewWifiWarning, strlen_P(webBodySettingsNewWifiWarning));
    webserver_send_content_P(client, refreshMeta, strlen_P(refreshMeta));
    webserver_send_content_P(client, webFooter, strlen_P(webFooter));
  } else if (client->content == size + 4) {
    setupWifi(heishamonSettings);
  }

  return 0;
}

int getSettings(struct webserver_t *client, settingsStruct *heishamonSettings) {
  switch (client->content) {
    case 0: {
        webserver_send(client, 200, (char *)"application/json", 0);
      } break;
    case 1: {
        /*
         * Make sure we stay within 128 bytes
         * (with respect of the maximum value length)
         */
        client->sendlist[0].size =
        snprintf_P((char *)client->sendlist[0].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%.*s%s%.*s"),
          PSTR("{\"wifi_hostname\":\""),
          strlen(heishamonSettings->wifi_hostname), heishamonSettings->wifi_hostname,
          PSTR("\",\"wifi_ssid\":\""),
          strlen(heishamonSettings->wifi_ssid), heishamonSettings->wifi_ssid
        );

        client->sendlist[1].size =
        snprintf_P((char *)client->sendlist[1].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%.*s%s"),
          PSTR("\",\"wifi_password\":\""),
          strlen(heishamonSettings->wifi_password), heishamonSettings->wifi_password,
          PSTR("\",\"current_ota_password\":\"")
        );

        client->sendlist[2].size =
        snprintf_P((char *)client->sendlist[2].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%.*s%s%.*s"),
          PSTR("\",\"mqtt_topic_base\":\""),
          strlen(heishamonSettings->mqtt_topic_base), heishamonSettings->mqtt_topic_base,
          PSTR("\",\"mqtt_server\":\""),
          strlen(heishamonSettings->mqtt_server), heishamonSettings->mqtt_server
        );

        client->sendlist[3].size =
        snprintf_P((char *)client->sendlist[3].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%.*s%s%d%s%d"),
          PSTR("\",\"mqtt_username\":\""),
          strlen(heishamonSettings->mqtt_username), heishamonSettings->mqtt_username,
          PSTR("\",\"updateAllTime\":"),
          heishamonSettings->updateAllTime,
          PSTR(",\"listenonly\":"),
          heishamonSettings->listenonly
        );

        client->sendlist[4].size =
        snprintf_P((char *)client->sendlist[4].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%.*s%s%d%s%d"),
          PSTR(",\"mqtt_password\":\""),
          strlen(heishamonSettings->mqtt_password), heishamonSettings->mqtt_password,
          PSTR("\",\"logHexdump\":"),
          heishamonSettings->logHexdump,
          PSTR(",\"logSerial1\":"),
          heishamonSettings->logSerial1
        );
      } break;
    case 2: {
        client->sendlist[0].size =
        snprintf_P((char *)client->sendlist[0].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%.*s%s%d%s%d%s%d%s%d%s%d"),
          PSTR(",\"mqtt_port\":\""),
          strlen(heishamonSettings->mqtt_port), heishamonSettings->mqtt_port,
          PSTR("\",\"timezone\":"),
          heishamonSettings->timezone,
          PSTR(",\"waitTime\":"),
          heishamonSettings->waitTime,
          PSTR(",\"logMqtt\":"),
          heishamonSettings->logMqtt,
          PSTR(",\"optionalPCB\":"),
          heishamonSettings->optionalPCB,
          PSTR(",\"use_1wire\":"),
          heishamonSettings->use_1wire
        );

        uint8_t i = 0;
        if (heishamonSettings->s0Settings[i].gpiopin == 255) heishamonSettings->s0Settings[i].gpiopin = DEFAULT_S0_PIN_1;  //dirty hack

        client->sendlist[1].size =
        snprintf_P((char *)client->sendlist[1].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%d%s%d%s%d%s%d%s%d"),
          PSTR(",\"waitDallasTime\":"),
          heishamonSettings->waitDallasTime,
          PSTR(",\"updataAllDallasTime\":"),
          heishamonSettings->updataAllDallasTime,
          PSTR(",\"dallasResolution\":"),
          heishamonSettings->dallasResolution,
          PSTR(",\"use_s0\":"),
          heishamonSettings->use_s0,
          PSTR(",\"s0_1_gpio\":"),
          heishamonSettings->s0Settings[i].gpiopin
        );

        client->sendlist[2].size =
        snprintf_P((char *)client->sendlist[2].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%d%s%d%s%d%s%d%s%d"),
          PSTR(",\"s0_1_ppkwh\":"),
          heishamonSettings->s0Settings[i].ppkwh,
          PSTR(",\"s0_1_interval\":"),
          heishamonSettings->s0Settings[i].lowerPowerInterval,
          PSTR(",\"s0_1_minpulsewidth\":"),
          heishamonSettings->s0Settings[i].minimalPulseWidth,
          PSTR(",\"s0_1_maxpulsewidth\":"),
          heishamonSettings->s0Settings[i].maximalPulseWidth,
          PSTR(",\"s0_1_minwatt\":"),
          (int) round((3600 * 1000 / heishamonSettings->s0Settings[i].ppkwh) / heishamonSettings->s0Settings[i].lowerPowerInterval)
        );

        i = 1;

        if (heishamonSettings->s0Settings[i].gpiopin == 255) heishamonSettings->s0Settings[i].gpiopin = DEFAULT_S0_PIN_2;  //dirty hack

        client->sendlist[3].size =
        snprintf_P((char *)client->sendlist[3].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%d%s%d%s%d%s%d%s%d"),
          PSTR(",\"s0_2_gpio\":"),
          heishamonSettings->s0Settings[i].gpiopin,
          PSTR(",\"s0_2_ppkwh\":"),
          heishamonSettings->s0Settings[i].ppkwh,
          PSTR(",\"s0_2_interval\":"),
          heishamonSettings->s0Settings[i].lowerPowerInterval,
          PSTR(",\"s0_2_minpulsewidth\":"),
          heishamonSettings->s0Settings[i].minimalPulseWidth,
          PSTR(",\"s0_2_maxpulsewidth\":"),
          heishamonSettings->s0Settings[i].maximalPulseWidth
        );

        client->sendlist[4].size =
        snprintf_P((char *)client->sendlist[4].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%s%d%s%s"),
          PSTR(",\"s0_2_minwatt\":"),
          (int) round((3600 * 1000 / heishamonSettings->s0Settings[i].ppkwh) / heishamonSettings->s0Settings[i].lowerPowerInterval),
          PSTR(",\"new_ota_password\":\""),
          PSTR("\",\"ntp_servers\":\"")
        );
      } break;
    case 3: {
        client->sendlist[0].size =
        snprintf_P((char *)client->sendlist[0].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%.*s%s"),
          strlen(heishamonSettings->ntp_servers), heishamonSettings->ntp_servers,
          PSTR("\"}")
          );
      } break;
  }
  return 0;
}

int handleSettings(struct webserver_t *client) {
  uint16_t size = sizeof(tzdata) / sizeof(tzdata[0]);
  if (client->content == 0) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else if (client->content == 1) {
    webserver_send_content_P(client, webHeader, strlen_P(webHeader));
    webserver_send_content_P(client, webCSS, strlen_P(webCSS));
    webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
    webserver_send_content_P(client, webBodySettings1, strlen_P(webBodySettings1));
    webserver_send_content_P(client, settingsForm1, strlen_P(settingsForm1));
  } else if (client->content >= 2 && client->content < size + 2) {
    tzStruct tz;
    memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
    client->sendlist[0].size =
    snprintf_P((char *)client->sendlist[0].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
      PSTR("<option value=\"%d\">%.*s</option>"),
      client->content - 2,
      strlen(tz.name), tz.name
    );
    client->content++;

    if(client->content < size + 2) {
      memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
      client->sendlist[1].size =
      snprintf_P((char *)client->sendlist[1].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
        PSTR("<option value=\"%d\">%.*s</option>"),
        client->content - 2,
        strlen(tz.name), tz.name
      );
      client->content++;
    }


    if(client->content < size + 2) {
      memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
      client->sendlist[2].size =
      snprintf_P((char *)client->sendlist[2].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
        PSTR("<option value=\"%d\">%.*s</option>"),
        client->content - 2,
        strlen(tz.name), tz.name
      );
      client->content++;
    }


    if(client->content < size + 2) {
      memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
      client->sendlist[3].size =
      snprintf_P((char *)client->sendlist[3].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
        PSTR("<option value=\"%d\">%.*s</option>"),
        client->content - 2,
        strlen(tz.name), tz.name
      );
      client->content++;
    }

    if(client->content < size + 2) {
      memcpy_P(&tz, &tzdata[client->content - 2], sizeof(tz));
      client->sendlist[4].size =
      snprintf_P((char *)client->sendlist[4].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
        PSTR("<option value=\"%d\">%.*s</option>"),
        client->content - 2,
        strlen(tz.name), tz.name
      );
      client->content++;
    }
    client->content--;
  } else if (client->content == size + 2) {
    webserver_send_content_P(client, settingsForm2, strlen_P(settingsForm2));
    webserver_send_content_P(client, menuJS, strlen_P(menuJS));
    webserver_send_content_P(client, settingsJS, strlen_P(settingsJS));
    webserver_send_content_P(client, populatescanwifiJS, strlen_P(populatescanwifiJS));
    webserver_send_content_P(client, changewifissidJS, strlen_P(changewifissidJS));
  } else if (client->content == size + 3) {
    webserver_send_content_P(client, populategetsettingsJS, strlen_P(populategetsettingsJS));
    webserver_send_content_P(client, webFooter, strlen_P(webFooter));
  }

  return 0;
}

int handleWifiScan(struct webserver_t *client) {
  if (client->content == 0) {
    webserver_send(client, 200, (char *)"application/json", 0);
    char *str = (char *)wifiJsonList.c_str();
    uint16_t len = strlen(str);

    char *cpy = strncpy((char *)client->sendlist[0].data.fixed, str, WEBSERVER_SENDLIST_BUFSIZE);
    client->sendlist[0].size = strlen(cpy);

    if(len > WEBSERVER_SENDLIST_BUFSIZE) {
      cpy = strncpy((char *)client->sendlist[1].data.fixed, &str[WEBSERVER_SENDLIST_BUFSIZE], WEBSERVER_SENDLIST_BUFSIZE);
      client->sendlist[1].size = strlen(cpy);
    }

    if(len > WEBSERVER_SENDLIST_BUFSIZE*2) {
      cpy = strncpy((char *)client->sendlist[2].data.fixed, &str[WEBSERVER_SENDLIST_BUFSIZE*2], WEBSERVER_SENDLIST_BUFSIZE);
      client->sendlist[2].size = strlen(cpy);
    }

    if(len > WEBSERVER_SENDLIST_BUFSIZE*3) {
      cpy = strncpy((char *)client->sendlist[3].data.fixed, &str[WEBSERVER_SENDLIST_BUFSIZE*3], WEBSERVER_SENDLIST_BUFSIZE);
      client->sendlist[3].size = strlen(cpy);
    }

    if(len > WEBSERVER_SENDLIST_BUFSIZE*4) {
      cpy = strncpy((char *)client->sendlist[4].data.fixed, &str[WEBSERVER_SENDLIST_BUFSIZE*4], WEBSERVER_SENDLIST_BUFSIZE);
      client->sendlist[4].size = strlen(cpy);
    }
  }
  //initatie a new async scan for next try
  WiFi.scanNetworksAsync(getWifiScanResults);
  return 0;
}

int handleDebug(struct webserver_t *client, char *hex, byte hex_len) {
  if (client->content == 0) {
    webserver_send(client, 200, (char *)"text/plain", 0);
    char log_msg[254];


#define LOGHEXBYTESPERLINE 32
    for (int i = 0; i < hex_len; i += LOGHEXBYTESPERLINE) {
      char buffer [(LOGHEXBYTESPERLINE * 3) + 1];
      buffer[LOGHEXBYTESPERLINE * 3] = '\0';
      for (int j = 0; ((j < LOGHEXBYTESPERLINE) && ((i + j) < hex_len)); j++) {
        sprintf(&buffer[3 * j], PSTR("%02X "), hex[i + j]);
      }
      uint8_t len = sprintf_P(log_msg, PSTR("data: %s\n"), buffer);
      webserver_send_content(client, log_msg, len);
    }
  }

  return 0;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      break;
    case WStype_CONNECTED: {
      } break;
    case WStype_TEXT:
      break;
    case WStype_BIN:
      break;
    case WStype_PONG: {
      } break;
    default:
      break;
  }
}

int handleRoot(struct webserver_t *client, float readpercentage, int mqttReconnects, settingsStruct *heishamonSettings) {
  switch (client->content) {
    case 0: {
        webserver_send(client, 200, (char *)"text/html", 0);
      } break;
    case 1: {
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
        webserver_send_content_P(client, webBodyRoot1, strlen_P(webBodyRoot1));
        webserver_send_content_P(client, heishamon_version, strlen_P(heishamon_version));
      } break;
    case 2: {
        webserver_send_content_P(client, webBodyRoot2, strlen_P(webBodyRoot2));
        if (heishamonSettings->use_1wire) {
          webserver_send_content_P(client, webBodyRootDallasTab, strlen_P(webBodyRootDallasTab));
        }
        if (heishamonSettings->use_s0) {
          webserver_send_content_P(client, webBodyRootS0Tab, strlen_P(webBodyRootS0Tab));
        }
        webserver_send_content_P(client, webBodyRootConsoleTab, strlen_P(webBodyRootConsoleTab));
        webserver_send_content_P(client, webBodyEndDiv, strlen_P(webBodyEndDiv));
      } break;
    case 3: {
        webserver_send_content_P(client, webBodyRootStatusWifi, strlen_P(webBodyRootStatusWifi));

        client->sendlist[1].size =
        snprintf_P((char *)client->sendlist[1].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%d"), getWifiQuality());

        webserver_send_content_P(client, webBodyRootStatusMemory, strlen_P(webBodyRootStatusMemory));

        client->sendlist[3].size =
        snprintf_P((char *)client->sendlist[3].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%d"), getFreeMemory());

        webserver_send_content_P(client, webBodyRootStatusReceived, strlen_P(webBodyRootStatusReceived));
      } break;
    case 4: {
        client->sendlist[0].size =
        snprintf_P((char *)client->sendlist[0].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%d"), readpercentage);

        webserver_send_content_P(client, webBodyRootStatusReconnects, strlen_P(webBodyRootStatusReconnects));

        client->sendlist[2].size =
        snprintf_P((char *)client->sendlist[2].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("%d"), mqttReconnects);

        webserver_send_content_P(client, webBodyRootStatusUptime, strlen_P(webBodyRootStatusUptime));

        char *up = getUptime();
        webserver_send_content(client, up, strlen(up));
        free(up);
      } break;
    case 5: {
        webserver_send_content_P(client, webBodyEndDiv, strlen_P(webBodyEndDiv));
        webserver_send_content_P(client, webBodyRootHeatpumpValues, strlen_P(webBodyRootHeatpumpValues));
        if (heishamonSettings->use_1wire) {
          webserver_send_content_P(client, webBodyRootDallasValues, strlen_P(webBodyRootDallasValues));
        }
        if (heishamonSettings->use_s0) {
          webserver_send_content_P(client, webBodyRootS0Values, strlen_P(webBodyRootS0Values));
        }
        webserver_send_content_P(client, webBodyRootConsole, strlen_P(webBodyRootConsole));
      } break;
    case 6: {
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
        webserver_send_content_P(client, refreshJS, strlen_P(refreshJS));
        webserver_send_content_P(client, selectJS, strlen_P(selectJS));
        webserver_send_content_P(client, websocketJS, strlen_P(websocketJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
  }
  return 0;
}

int handleTableRefresh(struct webserver_t *client, String actData[]) {
  int ret = 0;

  if (client->route == 11) {
    if (client->content == 0) {
      webserver_send(client, 200, (char *)"text/html", 0);
    } else if (client->content == 1) {
      dallasTableOutput(client);
    }
  } else if (client->route == 12) {
    if (client->content == 0) {
      webserver_send(client, 200, (char *)"text/html", 0);
    } else if (client->content == 1) {
      s0TableOutput(client);
    }
  } else if (client->route == 10) {
    if (client->content == 0) {
      webserver_send(client, 200, (char *)"text/html", 0);
    }
    if (client->content < NUMBER_OF_TOPICS) {
      uint8_t i = 0, x = client->content;
      for (uint8_t topic = x; topic < NUMBER_OF_TOPICS && topic < x + 5; topic++) {
        if(topic < NUMBER_OF_TOPICS) {
          char *str = (char *)actData[topic].c_str();
          int maxvalue = atoi(topicDescription[topic][0]);
          int value = actData[topic].toInt();

          client->sendlist[i].size =
          snprintf_P((char *)client->sendlist[i].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
            PSTR("<tr><td>TOP%d</td><td>%.*s</td><td>%.*s</td><td>%.*s</td></tr>"),
            topic,
            strlen_P(topics[topic]), topics[topic],
            strlen(str), str,
            (((value < 0) || (value > maxvalue)) ? strlen_P(_unknown) : strlen_P(topicDescription[topic][value + 1])),
            (((value < 0) || (value > maxvalue)) ? _unknown : topicDescription[topic][value + 1])
          );
          i++;
        }
        client->content++;
      }
      client->content--;
    }
  }
  return 0;
}

int handleJsonOutput(struct webserver_t *client, String actData[]) {
  if (client->content == 0) {
    webserver_send(client, 200, (char *)"application/json", 0);
  } else if(client->content == 1) {
    webserver_send_content_P(client, PSTR("{\"heatpump\":["), 13);
  } else if(client->content < NUMBER_OF_TOPICS+1) {
    uint8_t i = 0, x = client->content-1;
    for (uint8_t topic = x; topic < NUMBER_OF_TOPICS && topic < x + 5; topic++) {
      if(topic < NUMBER_OF_TOPICS) {
        char *str = (char *)actData[topic].c_str();
        int maxvalue = atoi(topicDescription[topic][0]);
        int value = actData[topic].toInt();

        client->sendlist[i].size =
        snprintf_P((char *)client->sendlist[i].data.fixed, WEBSERVER_SENDLIST_BUFSIZE,
          PSTR("{\"Topic\":\"TOP%d\",\"Name\":\"%.*s\",\"Value\":\"%.*s\",\"Description\":\"%.*s\"}%s"),
          topic,
          strlen_P(topics[topic]), topics[topic],
          strlen(str), str,
          (((value < 0) || (value > maxvalue)) ? strlen_P(_unknown) : strlen_P(topicDescription[topic][value + 1])),
          (((value < 0) || (value > maxvalue)) ? _unknown : topicDescription[topic][value + 1]),
          ((topic < NUMBER_OF_TOPICS - 1) ? "," : "")
        );
        i++;
      }
      client->content++;
    }
    client->content--;
  } else {
    if (client->content == NUMBER_OF_TOPICS + 1) {
      webserver_send_content_P(client, PSTR("],\"1wire\":"), 10);
      return 0;
    }
    if (client->content <= NUMBER_OF_TOPICS + 2) {
      dallasJsonOutput(client);
      if(client->content <= NUMBER_OF_TOPICS + 2) {
        webserver_send_content_P(client, PSTR("[]"), 2);
      }
      return 0;
    }
    if (client->content <= NUMBER_OF_TOPICS + 3) {
      webserver_send_content_P(client, PSTR(",\"s0\":"), 6);
      return 0;
    }
    if (client->content <= NUMBER_OF_TOPICS + 4) {
      if(client->content <= NUMBER_OF_TOPICS + 4) {
        webserver_send_content_P(client, PSTR("[]"), 2);
      }
      return 0;
    }
    if (client->content == NUMBER_OF_TOPICS + 5) {
      webserver_send_content_P(client, PSTR("}"), 1);
      return 0;
    }
  }
  return 0;
}

int showRules(struct webserver_t *client) {
  uint16_t len = 0, len1 = 0;

  if (client->content == 0) {
    webserver_send(client, 200, (char *)"text/html", 0);
    webserver_send_content_P(client, webHeader, strlen_P(webHeader));
    webserver_send_content_P(client, webCSS, strlen_P(webCSS));
    webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
    webserver_send_content_P(client, showRulesPage1, strlen_P(showRulesPage1));
    if (LittleFS.begin()) {
      client->userdata = new fs::File(LittleFS.open("/rules.txt", "r"));
    }
  } else if (client->userdata != NULL) {
    uint8_t i = 0;
    for(i=0;i<WEBSERVER_MAX_SENDLIST;i++) {
#define BUFFER_SIZE WEBSERVER_SENDLIST_BUFSIZE
      File *f = (File *)client->userdata;
      if (f && *f) {
        len = f->size();
      }

      if (len > 0) {
        f->seek((client->content - 1)*BUFFER_SIZE, SeekSet);
        if (client->content * BUFFER_SIZE <= len) {
          f->readBytes((char *)client->sendlist[i].data.fixed, BUFFER_SIZE);
          len1 = BUFFER_SIZE;
        } else if ((client->content * BUFFER_SIZE) >= len && (client->content * BUFFER_SIZE) <= len + BUFFER_SIZE) {
          f->readBytes((char *)client->sendlist[i].data.fixed, len - ((client->content - 1)*BUFFER_SIZE));
          len1 = len - ((client->content - 1) * BUFFER_SIZE);
        } else {
          len1 = 0;
        }

        if (len1 > 0) {
          if (len1 < BUFFER_SIZE) {
            if (f) {
              if (*f) {
                f->close();
              }
              delete f;
            }
            client->userdata = NULL;
            webserver_send_content_P(client, showRulesPage2, strlen_P(showRulesPage2));
            webserver_send_content_P(client, menuJS, strlen_P(menuJS));
            webserver_send_content_P(client, webFooter, strlen_P(webFooter));
            break;
          }
        } else if (client->content == 1) {
          if (f) {
            if (*f) {
              f->close();
            }
            delete f;
          }
          client->userdata = NULL;
          webserver_send_content_P(client, showRulesPage2, strlen_P(showRulesPage2));
          webserver_send_content_P(client, menuJS, strlen_P(menuJS));
          webserver_send_content_P(client, webFooter, strlen_P(webFooter));
          break;
        }
      } else if (client->content == 1) {
        if (f) {
          if (*f) {
            f->close();
          }
          delete f;
        }
        client->userdata = NULL;
        webserver_send_content_P(client, showRulesPage2, strlen_P(showRulesPage2));
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
        break;
      }
      client->content++;
    }
    client->content--;
  } else if (client->content == 1) {
    webserver_send_content_P(client, showRulesPage2, strlen_P(showRulesPage2));
    webserver_send_content_P(client, menuJS, strlen_P(menuJS));
    webserver_send_content_P(client, webFooter, strlen_P(webFooter));
  }

  return 0;
}

int showFirmware(struct webserver_t *client) {
  if (client->content == 0) {
    webserver_send(client, 200, (char *)"text/html", 0);
    webserver_send_content_P(client, webHeader, strlen_P(webHeader));
    webserver_send_content_P(client, webCSS, strlen_P(webCSS));
    webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
  } else if (client->content == 1) {
    webserver_send_content_P(client, showFirmwarePage, strlen_P(showFirmwarePage));
    webserver_send_content_P(client, menuJS, strlen_P(menuJS));
    webserver_send_content_P(client, webFooter, strlen_P(webFooter));
  }
  return 0;
}

int showFirmwareSuccess(struct webserver_t *client) {
  if (client->content == 0) {
    webserver_send(client, 200, (char *)"text/html", strlen_P(firmwareSuccessResponse));
    webserver_send_content_P(client, firmwareSuccessResponse, strlen_P(firmwareSuccessResponse));
  }
  return 0;
}

static void printUpdateError(char **out, uint8_t size) {
  uint8_t len = 0;
  len = snprintf_P(*out, size, PSTR("<br />ERROR[%u]: "), Update.getError());
  if (Update.getError() == UPDATE_ERROR_OK) {
    snprintf_P(&(*out)[len], size - len, PSTR("No Error"));
  } else if (Update.getError() == UPDATE_ERROR_WRITE) {
    snprintf_P(&(*out)[len], size - len, PSTR("Flash Write Failed"));
  } else if (Update.getError() == UPDATE_ERROR_ERASE) {
    snprintf_P(&(*out)[len], size - len, PSTR("Flash Erase Failed"));
  } else if (Update.getError() == UPDATE_ERROR_READ) {
    snprintf_P(&(*out)[len], size - len, PSTR("Flash Read Failed"));
  } else if (Update.getError() == UPDATE_ERROR_SPACE) {
    snprintf_P(&(*out)[len], size - len, PSTR("Not Enough Space"));
  } else if (Update.getError() == UPDATE_ERROR_SIZE) {
    snprintf_P(&(*out)[len], size - len, PSTR("Bad Size Given"));
  } else if (Update.getError() == UPDATE_ERROR_STREAM) {
    snprintf_P(&(*out)[len], size - len, PSTR("Stream Read Timeout"));
#ifdef UPDATE_ERROR_NO_DATA
  } else if (Update.getError() == UPDATE_ERROR_NO_DATA) {
    snprintf_P(&(*out)[len], size - len, PSTR("No data supplied"));
#endif
  } else if (Update.getError() == UPDATE_ERROR_MD5) {
    snprintf_P(&(*out)[len], size - len, PSTR("MD5 Failed\n"));
  } else if (Update.getError() == UPDATE_ERROR_SIGN) {
    snprintf_P(&(*out)[len], size - len, PSTR("Signature verification failed"));
  } else if (Update.getError() == UPDATE_ERROR_FLASH_CONFIG) {
    snprintf_P(&(*out)[len], size - len, PSTR("Flash config wrong real: %d IDE: %d\n"), ESP.getFlashChipRealSize(), ESP.getFlashChipSize());
  } else if (Update.getError() == UPDATE_ERROR_NEW_FLASH_CONFIG) {
    snprintf_P(&(*out)[len], size - len, PSTR("new Flash config wrong real: %d\n"), ESP.getFlashChipRealSize());
  } else if (Update.getError() == UPDATE_ERROR_MAGIC_BYTE) {
    snprintf_P(&(*out)[len], size - len, PSTR("Magic byte is wrong, not 0xE9"));
  } else if (Update.getError() == UPDATE_ERROR_BOOTSTRAP) {
    snprintf_P(&(*out)[len], size - len, PSTR("Invalid bootstrapping state, reset ESP8266 before updating"));
  } else {
    snprintf_P(&(*out)[len], size - len, PSTR("UNKNOWN"));
  }
}


int showFirmwareFail(struct webserver_t *client) {
  if (client->content == 0) {
    char str[256] = { '\0' }, *p = str;
    printUpdateError(&p, sizeof(str));

    webserver_send(client, 200, (char *)"text/html", strlen_P(firmwareFailResponse) + strlen(str));
    webserver_send_content_P(client, firmwareFailResponse, strlen_P(firmwareFailResponse));
    webserver_send_content(client, str, strlen(str));
  }
  return 0;
}
