/******************************************************************************
ESP node with MQTT and Telegram bot based interaction


Features: + Temperature and Humidity monitoring via MQTT
          + Nokia 5110 display for local monitoring
          + Integrated Timer
          + NTP function with support for CEST


Hardware: ESP-12E + DHT22
******************************************************************************/
#include "libs.h"
#include "DebugUtils.h"
#include "credentials.h"

#ifdef DHT
// DHT SETTINGS
#define DHTPIN 5
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
void readDHT() {
  // Wait at least 2 seconds seconds between measurements.
  // Reading temperature and humidity takes about 250 milliseconds!
  hum = dht.readHumidity();         // Read humidity (percent)
  temp = dht.readTemperature();     // Read temperature in Degrees Celsius
  // Check if any reads failed and exit early (to try again).
  if (isnan(hum) || isnan(temp))
  {
    Serial.print(F("Failed to read from DHT sensor!"));
    return;
  }
}
#endif

// Read vcc
float readVccCalibrated(){
  float vcc = ESP.getVcc();
  vcc = vcc / 969;
  return vcc;
}

// Pin for switching things ON and OFF
#define SWITCHPIN 15

// global variables
uint16_t timer_val = 0;
uint8_t seconds;
uint8_t last_second = 99;
uint16_t last_minute = 99;
uint8_t i = 0;
String chat_id;
//uint16_t t3,t4;
//uint8_t minutesUntilItIsTime = 0;
float hum, temp;
bool newStateOfSwitch, stateOfSwitch = 99;

#ifdef TELEGRAM
WiFiClientSecure espClient;
bool expectingTemp = false;
bool alarmActive = true;
uint8_t alarmTemp = 60;
const char an[] PROGMEM = "AN";
const char aus[] PROGMEM = "AUS";
const char fail[] PROGMEM = "Fehler!";
const char ok[] PROGMEM = "OK!";
//WiFiClientSecure BOTclient;
//UniversalTelegramBot bot(botToken, espClient);
UniversalTelegramBot *bot;
void handleNewMessages(int numNewMessages);
#endif

ADC_MODE(ADC_VCC); // to be able to use ESP.getVcc()

#ifdef NTP
WiFiUDP ntpUDP;
// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
time_t ntpSyncProvider() {
  return timeClient.getEpochTime();
}
// vereinfachte Umsetzung der CEST (Umstellung jeweils um 0:00 Uhr)
bool IsCEST(time_t t){
  int m = month(t);
  if (m < 3 || m > 10)  return false;
  if (m > 3 && m < 10)  return true;
  int previousSunday = day(t) - weekday(t);
  if (m == 3) return previousSunday >= 25;
  if (m == 10) return previousSunday < 25;
  return false; // this line should never gonna happen
}
void check4CEST(time_t t){
  if(IsCEST(t)){
    timeClient.setTimeOffset(7200);
    //Serial.println(F("CEST is on!"));
  }
  else{
    timeClient.setTimeOffset(3600);
    //Serial.println(F("CEST is off!"));
  }
}
#endif

#ifdef ASYNCMQTT
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void publishTempHumVccToMQTT(){
  char cBuff[6] = {0};
  uint8_t errorcode = 0;
  Serial.print(F("Sending..."));

  // Publish temperature
  Serial.print(" Tmp.: ");
  Serial.print(temp);
  dtostrf(temp,5,1,cBuff);

  if(!mqttClient.publish(USER PREAMBLE F_TEMP, 1, true, cBuff))
  {
    errorcode = errorcode | 1;
  }
  // Publish humidity
  Serial.print(" Hum.: ");
  Serial.print(hum);
  dtostrf(hum,5,1,cBuff);
  if(!mqttClient.publish(USER PREAMBLE F_HUM, 1, true, cBuff))
  {
    errorcode = errorcode | 1 << 1;
  }
  // Publish voltage
  Serial.print(" VCC: ");
  Serial.println(readVccCalibrated());
  dtostrf(readVccCalibrated(),5,1,cBuff);
  if(!mqttClient.publish(USER PREAMBLE F_VCC, 1, true, cBuff))
  {
    errorcode = errorcode | 1 << 2;
  }
  if (errorcode){Serial.print(errorcode,BIN);}else{Serial.print(FPSTR(ok));}
}
void publishTimerAndStateToMQTT(){
  char cBuff[5] = {0};
  Serial.print("Status: ");
  Serial.print(stateOfSwitch ? FPSTR(an) : FPSTR(aus));

  String buff = (stateOfSwitch ? FPSTR(an) : FPSTR(aus));
  buff.toCharArray(cBuff,4);
  if(!mqttClient.publish(USER PREAMBLE F_STATE, 1, true, cBuff))
  {
    Serial.println(FPSTR(fail));
  }
  //publish timer value
  Serial.print(" - Timer: ");
  Serial.println(timer_val);
  itoa(timer_val,cBuff,10);
  //dtostrf(timer_val,3,0,cBuff);
  Serial.println(cBuff);
  if(!mqttClient.publish(USER PREAMBLE F_TIMER, 1, true, cBuff))
  {
    Serial.println(FPSTR(fail));
  }
  else{Serial.println(FPSTR(ok));}
}
void handleMqttMessage(char* topic, char* payload, size_t len){
  if (strcmp(topic,USER PREAMBLE F_TIMER) == 0){
    uint8_t val = atoi((char*)payload);
    Serial.print(F("MQTT -> "));
    Serial.println(val);
    if (val == 0){newStateOfSwitch = 0;}
    else if (val > 0){newStateOfSwitch = 1;}
    else{newStateOfSwitch = 0;}
    timer_val = val;
  }
}

void connectToWifi() {
  //Serial.println("Connecting to Wi-Fi...");
  //WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}
void connectToMqtt() {
  Serial.println(F("Connecting to MQTT..."));
  mqttClient.connect();
}
void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println(F("Connected to Wi-Fi."));
  connectToMqtt();
}
void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println(F("Disconnected from Wi-Fi."));
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}
void onMqttConnect(bool sessionPresent) {
  Serial.println(F("Connected to MQTT."));
  Serial.print(F("Session present: "));
  Serial.println(sessionPresent);

  uint16_t packetIdSub = mqttClient.subscribe(USER PREAMBLE F_TIMER, 1);
  Serial.print(F("Subscribing at QoS 1, packetId: "));
  Serial.println(packetIdSub);

  /*
  mqttClient.publish(USER PREAMBLE F_TIMER, 0, true, "1");
  Serial.println("Publishing at QoS 0");

  uint16_t packetIdPub1 = mqttClient.publish(USER PREAMBLE F_TIMER, 1, true, "2");
  Serial.print("Publishing at QoS 1, packetId: ");
  Serial.println(packetIdPub1);
  */

  uint16_t packetIdPub2 = mqttClient.publish(USER PREAMBLE F_STATE, 1, true, "alive!");
  //Serial.print("Publishing at QoS 2, packetId: ");
  //Serial.println(packetIdPub2);
}
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println(F("Disconnected from MQTT."));

  if (reason == AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT) {
    Serial.println(F("Bad server fingerprint."));
  }

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}
void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println(F("Subscribe acknowledged."));
  //Serial.print("  packetId: ");
  //Serial.println(packetId);
  //Serial.print("  qos: ");
  //Serial.println(qos);
}
void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println(F("Unsubscribe acknowledged."));
  //Serial.print("  packetId: ");
  //Serial.println(packetId);
}
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  // Serial.println(F("Publish received."));
  // Serial.print(F("  topic: "));
  // Serial.println(topic);
  // Serial.print(F("  qos: "));
  // Serial.println(properties.qos);
  /*
  Serial.print(F("  dup: "));
  Serial.println(properties.dup);
  Serial.print(F("  retain: "));
  Serial.println(properties.retain);
  Serial.print(F("  len: "));
  Serial.println(len);
  Serial.print(F("  index: "));
  Serial.println(index);
  */
  // Serial.print(F("  total: "));
  // Serial.println(total);
  handleMqttMessage(topic,payload,len);

}
void onMqttPublish(uint16_t packetId) {
  Serial.println(F("Publish acknowledged."));
  // Serial.print("  packetId: ");
  // Serial.println(packetId);
}
#endif


#ifdef MQTT
PubSubClient client(espClient);
void connect2MQTT(){
  const char* host = MQTT_SERVER;
  Serial.print(F("Connecting to "));
  Serial.print(host);
  if (! espClient.connect(host, MQTT_PORT)) {
    Serial.print(F("Connection failed. Halting execution."));
    while(1);
  }
  if (espClient.verify(fingerprint, host)) {
    Serial.print(F("Connection secure."));
  } else {
    Serial.println(F("Connection insecure! Halting execution."));
    while(1);
  }
}
void publishData(){
  String buff;
  char valStr[5];
  uint8_t errorcode = 0;
  Serial.print("Sending:");
  // Publish temperature
  Serial.print("Tmp.: ");
  Serial.print(temp);
  buff =  (String)temp;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_TEMP, valStr))
  {
    errorcode = errorcode | 1;
  }
  // Publish humidity
  Serial.print("Hum.: ");
  Serial.print(hum);
  buff =  (String)hum;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_HUM, valStr))
  {
    errorcode = errorcode | 1 << 1;
  }
  // Publish voltage
  Serial.print("VCC: ");
  Serial.print(vcc);
  buff =  (String)vcc;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_VCC, valStr))
  {
    errorcode = errorcode | 1 << 2;
  }
  if (errorcode){Serial.print(errorcode,BIN);}else{Serial.print(FPSTR(ok));}
}
void publishStatus(){
  char valStr[5];
  Serial.print("Status: ");
  Serial.print(stateOfSwitch ? FPSTR(AN) : FPSTR(AUS));
  String buff = (String)(stateOfSwitch ? FPSTR(AN) : FPSTR(AUS)) ;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_STATE, valStr))
  {
    Serial.print(FPSTR(fail));
  }
  //publish timer value
  Serial.print("Timer: ");
  Serial.print(timer_val);
  buff = (String)timer_val;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_TIMER, valStr))
  {
    Serial.print(FPSTR(fail));
  }
  else{Serial.print(FPSTR(ok));}
}
void callback(char* topic, byte* payload, unsigned int length){

  Serial.print("-> ");

  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.print("");

  //Serial.print("length: ");
  //Serial.print(length);

  //converting value to
  int val = 0;
  for (int i=0; i<length; i++){
    val = val*10 + (payload[i])-48;
    //Serial.print(val);
  }
  //val = atoi((char*)payload);

  Serial.print(val);

  if (val == 0){newStateOfSwitch = 0;}
  else if (val > 0){newStateOfSwitch = 1;}
  else{newStateOfSwitch = 0;}
  timer_val = val;
}
void connect2MQTTBroker(){
  // Loop until we're reconnected
  if (!client.connected()) {
    Serial.print("\n.");
    // Attempt to connect
    if (client.connect("", USER, PASS, USER PREAMBLE F_CON, 1, 1, "OFFLINE")) {
      Serial.print("!");
      // Once connected, publish an announcement...
      client.publish(USER PREAMBLE F_CON,"ONLINE");
      // ... and resubscribe
      client.subscribe(USER PREAMBLE F_TIMER,1);
    } else {
      Serial.print(F("failed, rc="));
      Serial.print(client.state());
      Serial.print(F(" try again in 5 seconds"));
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
#endif

#ifdef WIFIMANAGER
//flag for saving data
bool shouldSaveConfig = false;
//callback notifying us of the need to save config
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}
// void readBotTokenFromEeprom(int offset){
//   for(int i = offset; i<BOT_TOKEN_LENGTH; i++ ){
//     botToken[i] = EEPROM.read(i);
//     Serial.println(botToken[i]);
//   }
//   EEPROM.commit();
// }
// void writeBotTokenToEeprom(int offset){
//   for(int i = offset; i<BOT_TOKEN_LENGTH; i++ ){
//     EEPROM.write(i, botToken[i]);
//     Serial.println(botToken[i]);
//   }
//   EEPROM.commit();
//   Serial.println("EEPROM commit");
// }
#else
void setup_wifi(){
  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  // Attempt to connect to Wifi network:
  Serial.print(F("Connecting Wifi: "));
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);
  // Restart if connection not possible
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println(F("Connection Failed! Rebooting..."));
    delay(8000);
    ESP.restart();
  }
  Serial.println(F("IP address: "));
  Serial.println(WiFi.localIP());
}
#endif


//function to prepend string with a zero if it's only single digit
String m2D(int s){
  String sReturn = "DD";
  if (s < 10){sReturn = ("0"+String(s));}
  else {sReturn = String(s);}
  return sReturn;
}

//Function to check whether given String is a valid number
bool isValidNumber(String str){
   for(byte i=0;i<str.length();i++){
     if(isDigit(str.charAt(i))) return true;
   }
   return false;
}

#ifdef N5110
//Function to update Nokia LCD
void updateLCD(){
  time_t tm = now();
  int len = 20;
  char buf [20];
  LCDClear();

  // go to first row
  gotoXY(0, 0);
  // print Time
  String sBuff = (m2D(hour(tm))+":"+m2D(minute(tm))+":"+m2D(second(tm)));
  sBuff.toCharArray(buf, len);
  LCDString(buf);
  // go to second row
  gotoXY(0, 1);
  // print Date on second row
  sBuff = (m2D(day(tm))+"."+m2D(month(tm))+"."+String(year(tm)));
  sBuff.toCharArray(buf, len);
  LCDString(buf);
  // go to 3rd row
  gotoXY(0, 2);
  // print temperature
  String t = String(temp);
  sBuff = (t + " Grad C");
  sBuff.toCharArray(buf, len);
  LCDString(buf);
  // go to 4th row
  gotoXY(0, 3);
  // print humidity
  String h = String(hum);
  sBuff = (h +" % r.F.");
  sBuff.toCharArray(buf, len);
  LCDString(buf);
  // go to 5th row
  gotoXY(0, 4);
  // print timer value and status
  t = String(timer_val);
  // set h to "AN" or "AUS" depending on stateOfSwitch
  h = stateOfSwitch ? FPSTR(an) : FPSTR(aus);
  sBuff = (t + " min - " + h);
  sBuff.toCharArray(buf, len);
  LCDString(buf);
  gotoXY(0, 5);
  // print free heap size
  t = String(ESP.getFreeHeap());
  t.toCharArray(buf, len);
  LCDString(buf);
}
#endif

#ifdef TELEGRAM
void handleNewMessages(int numNewMessages) {
  //Serial.println(F("No. Messages: "));
  //Serial.print(String(numNewMessages));
  for (int i=0; i<numNewMessages; i++) {
    chat_id = String(bot->messages[i].chat_id);
    String text = bot->messages[i].text;
    String from_name = bot->messages[i].from_name;
    String senderID = bot->messages[i].from_id;
    //if (from_name == "") from_name = "Guest";
    if (senderID == adminID) {
      Serial.println(text);
      if (expectingTemp){
        if(isValidNumber(text) && (text.toInt() >= 25) && (text.toInt() <= 90)){
          alarmTemp = text.toInt();
          alarmActive = true;
          bot->sendMessage(chat_id, (PGM_P)F("Benachrichtigung bei ") + text + " °C", "");
          expectingTemp = false;
        }
        else{
          bot->sendMessage(chat_id, F("Das ist keine gültige Temperatur!"), "");
          expectingTemp = false;
        }
      }
      else if (text == "/start") {
        String welcome = "Hi " + from_name + ",\n";
        welcome += F("Wie kann ich helfen?\n\n");
        welcome += F("/on : Einschalten\n");
        welcome += F("/off : Ausschalten\n");
        welcome += F("/status : Anzeige des akt. Status\n");
        welcome += F("/notify: Benachrichtigung bei bestimmter Temperatur\n");
        welcome += F("/silence: Benachrichtigungen deaktivieren\n");
        welcome += F("/options : alle Optionen\n");
        bot->sendMessage(chat_id, welcome, "Markdown");
      }
      else if (text == "/options") {
        String keyboardJson = F("[[\"/on\", \"/off\"],[\"/status\"],[\"/notify\", \"/silence\"]]");
        bot->sendMessageWithReplyKeyboard(chat_id, (PGM_P)(F("Wähle eine der folgenden Optionen:")), "", keyboardJson, true);
      }
      else if (text == "/on") {
        newStateOfSwitch = 1;
        timer_val = 30;
        bot->sendMessage(chat_id, FPSTR(an), "");
      }
      else if (text == "/off") {
        newStateOfSwitch = 0;
        timer_val = 0;
        bot->sendMessage(chat_id, FPSTR(aus), "");
      }
      else if (text == "/status") {
        String buff;
        buff = (String)temp;
        bot->sendMessage(chat_id, buff + " °C","");
        buff = (String)hum;
        bot->sendMessage(chat_id, buff + " % r.F.","");
        buff = (String)timer_val;
        bot->sendMessage(chat_id, (PGM_P)(F("Timer: ")) + buff + " min","");
        if(stateOfSwitch){
          bot->sendMessage(chat_id, FPSTR(an), "");
        } else {
          bot->sendMessage(chat_id, FPSTR(aus), "");
        }
        if (alarmActive){
          bot->sendMessage(chat_id, (PGM_P)(F("Nachricht bei ")) + String(alarmTemp) + " °C","");
        }
        else {bot->sendMessage(chat_id, (PGM_P)(F("Benachrichtigung deaktiviert!")),"");}
        buff = (String)ESP.getFreeHeap();
        bot->sendMessage(chat_id, (PGM_P)F("Heap frei: ") + buff,"");
      }
      else if (text == "/notify") {
        expectingTemp = true;
        bot->sendMessage(chat_id, F("OK, bei welcher Temperatur?"), "");
      }
      else if (text == "/silence") {
        alarmActive = false;
        bot->sendMessage(chat_id, (PGM_P)(F("Benachrichtigung deaktiviert!")),"");
      }
      else {
        bot->sendMessage(chat_id, "?!?: " + text, "");
        bot->sendPhoto(chat_id, F("https://ih1.redbubble.net/image.31887377.4850/fc,550x550,white.jpg"), F("...feels bad man..."));
      }
    }
    else {
      //bot->sendPhoto(chat_id, "https://ih1.redbubble.net/image.31887377.4850/fc,550x550,white.jpg", from_name + "Please leave me alone...");
      bot->sendMessage(chat_id, (PGM_P)F("Zugriff verweigert. Deine ID: ") + senderID, "");
    }
  }
}
#endif

#ifdef OTA
void setupOTA(){
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.print("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.print("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.print("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.print("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.print("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.print("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.print("End Failed");
  });
  ArduinoOTA.begin();
}
#endif

//function to detect change of minute, if so, return 1
bool minuteGone(){
  //check if minute changed
  uint8_t act_minute = minute();
  if (act_minute != last_minute){
    last_minute = act_minute;
    return 1;
  }
  //if minute isn't gone yet, return 0
  else{return 0;};
}

//function to determine change of second, if so, update LCD and return 1
bool secondGone(){
  uint8_t s = second();
  if (s != last_second){
    last_second = s;
    return 1;
  }
  else{return 0;};
}

//function to set output pin according to stateOfSwitch
void updateSwitchAndPublishIfChanged(){
  if (newStateOfSwitch != stateOfSwitch){
    if (newStateOfSwitch == 0){
      Serial.println(FPSTR(aus));
      digitalWrite(SWITCHPIN, LOW);
    }
    else if (newStateOfSwitch == 1){
      Serial.println(FPSTR(an));
      digitalWrite(SWITCHPIN, HIGH);
    }
    stateOfSwitch = newStateOfSwitch;

    #ifdef MQTT
    publishStatus();
    #endif

    #ifdef ASYNCMQTT
    publishTimerAndStateToMQTT();
    #endif

    #ifdef TELEGRAM
    String buff = (stateOfSwitch ? FPSTR(an) : FPSTR(aus));
    bot->sendMessage(chat_id, buff,"");
    #endif
  }
}

void updateTimerValueAndSwitchState(){
  //if timer =! 0, decrease timer value and set stateOfSwitch to 1 (=On)
  if (timer_val >= 1){
    timer_val--;
    newStateOfSwitch = 1;
  }
  //if timer = 0, set stateOfswitch to 0 (=Off)
  else if (timer_val <= 0){
    newStateOfSwitch = 0;
  }
}

// bool itIsTimetoPush(){
//   if (minutesUntilItIsTime <= 0) {
//     return true;
//     minutesUntilItIsTime = 5;
//   }
//   else {
//     minutesUntilItIsTime --;
//     return false;
//   }
// }

void PushViaTelegramIfHot(){
  if (temp > alarmTemp) {
  //  if (itIsTimetoPush()){
      String message = F("Sauna hat ");
      message.concat(temp);
      message.concat(" °C");
      if(bot->sendMessage(chat_id, message, "Markdown")){
        Serial.print(F("Benachrichtigung gesendet!"));
      }
   // }
  }
  //else minutesUntilItIsTime = 0;
}

void setup() {
  pinMode(SWITCHPIN, OUTPUT);
  Serial.begin(74880);

  #ifdef N5110
  LCDInit(); //Init the LCD
  LCDClear();
  #endif

  delay(500);

  Serial.println(F("Booting..."));
  Serial.printf("Flash size: %d Bytes \n", ESP.getFlashChipRealSize());

  #ifdef ASYNCMQTT
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(USER, PASS);
  mqttClient.setKeepAlive(KEEPALIVE);
  mqttClient.setWill(LW_TOPIC,LW_QOS,LW_RETAIN,LW_PAYLOAD,LW_LENGTH);

  #if ASYNC_TCP_SSL_ENABLED
  mqttClient.setSecure(MQTT_SECURE);
  if (MQTT_SECURE) {
    mqttClient.addServerFingerprint((const uint8_t[])MQTT_SERVER_FINGERPRINT);
  }
  #endif
  #endif

  #ifdef WIFIMANAGER
  //EEPROM.begin(BOT_TOKEN_LENGTH);
  //Serial.println("read bot token");
  //readBotTokenFromEeprom(0);
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  //Adding an additional config on the WIFI manager webpage for the bot token
  // WiFiManagerParameter custom_bot_id("botid", "Bot Token", botToken, 50);
  // wifiManager.addParameter(&custom_bot_id);
  //If it fails to connect it will create a access point
  wifiManager.setTimeout(300);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if(!wifiManager.autoConnect()) {
    Serial.println(F("failed to connect and hit timeout..."));
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  Serial.print(F("WiFi connected - IP address: "));
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);
  #else
  setup_wifi();
  #endif

  #ifdef TELEGRAM
  bot = new UniversalTelegramBot(botToken, espClient);
  #endif

  #ifdef MQTT
  connect2MQTT();
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  #endif

  #ifdef NTP
  timeClient.begin();
  setSyncProvider(&ntpSyncProvider);
  setSyncInterval(60);
  #endif

  #ifdef OTA
  setupOTA();
  #endif
}

void loop() {
  yield();
  if (WiFi.status() == 6) {ESP.reset();}
  //sync output pin every second
  if(secondGone()){
    updateSwitchAndPublishIfChanged();

    #ifdef N5110
    updateLCD();
    #endif

    #ifdef TELEGRAM
    int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
    while(numNewMessages) {
      //Serial.println(F("Nachricht erhalten!"));
      handleNewMessages(numNewMessages);
      numNewMessages = bot->getUpdates(bot->last_message_received + 1);
    }
    #endif
    seconds++;
  }

  //read DHT and vcc and publish to MQTT feeds every 10 s
  if(seconds >= 10){
    #ifdef DHT
    readDHT();
    #ifdef ASYNCMQTT
    publishTempHumVccToMQTT();
    #else
    Serial.println(readVccCalibrated());
    #endif
    #endif
    seconds = 0;
  }

  //update timer value and switch state every minute
  if (minuteGone()){
    updateTimerValueAndSwitchState();
    PushViaTelegramIfHot();
    #ifdef MQTT
    publishStatus();
    #endif
    #ifdef ASYNCMQTT
    publishTimerAndStateToMQTT();
    #endif
    //Serial.println(timeClient.getFormattedTime());
    check4CEST(now());
  }

  #ifdef OTA
  ArduinoOTA.handle();
  #endif

  #ifdef NTP
  timeClient.update();
  #endif

  #ifdef MQTT
  //if not yet, connect to MQTT broker
  if(!client.connected()){
    connect2MQTTBroker();
  }
  client.loop();
  #endif
}
