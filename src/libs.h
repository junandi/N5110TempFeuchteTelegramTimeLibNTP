#ifndef LIBS_H
#define LIBS_H

#define DEBUG
// Telegram and MQTT interface can't be used both
//#define MQTT
#define TELEGRAM
#define NTP
#define WIFIMANAGER
//#define OTA

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#ifdef NTP
#include <WiFiUdp.h>
#include <NTPClient.h>
#endif

#ifdef WIFIMANAGER
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#endif

#ifdef OTA
#include <ArduinoOTA.h>
#endif

#ifdef TELEGRAM
#include <UniversalTelegramBot.h>
#endif

#include <SPI.h>
#include <N5110.h>

#include <TimeLib.h>
//#include <WiFiClientSecure.h>
//#include <Task.h>
#include <ArduinoJson.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>

#ifdef MQTT
#include <PubSubClient.h>
#endif

#endif
