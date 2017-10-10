/******************************************************************************
ESP node with MQTT and Telegram bot based interaction


Features: + Temperature and Humidity monitoring via MQTT

Libraries: + Adafruit DHT and Unified Sensor
           + TimeLib
           + PubSubClient

******************************************************************************/

#include "DebugUtils.h"
#include "libs.h"
#include "credentials.h"

// DHT SETTINGS
#define DHTPIN 5
#define DHTTYPE DHT22

// ON/OFF BUTTON
#define SWITCHPIN 9

WiFiClientSecure espClient;

//MQTT
#ifdef MQTT
PubSubClient client(espClient);
void connect2MQTT();
void publishData();
#endif

//Telegram
#ifdef TELEGRAM
//WiFiClientSecure BOTclient;
UniversalTelegramBot bot(BOTtoken, espClient);
int Bot_mtbs = 1000; //mean time between scan messages
long Bot_lasttime;   //last time messages' scan has been done
void handleNewMessages(int numNewMessages);
#endif

//void digitalClockDisplay();
//void printDigits(int digits);

void updateLCD();
void readDHTAndPublishData();

#ifdef NTP
WiFiUDP ntpUDP;
// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
time_t ntpSyncProvider() {
  return timeClient.getEpochTime();
}
// vereinfachte Umsetzung der DST (Umstellung jeweils um 0:00 Uhr)
bool IsDst(time_t t){
  int m = month(t);
  if (m < 3 || m > 10)  return false;
  if (m > 3 && m < 10)  return true;
  int previousSunday = day(t) - weekday(t);
  if (m == 3) return previousSunday >= 25;
  if (m == 10) return previousSunday < 25;
  return false; // this line should never gonna happen
}
void check4CEST(time_t t){
  if(IsDst(t)){
    timeClient.setTimeOffset(7200);
    Serial.println("DST is on!");
  }
  else{
    timeClient.setTimeOffset(3600);
    Serial.println("DST is off!");
  }
}
#endif

DHT dht(DHTPIN, DHTTYPE);

ADC_MODE(ADC_VCC); // to be able to use ESP.getVcc()

// global variables
uint16_t timer_val = 0;
uint8_t seconds;
uint8_t last_second = 99;
uint16_t last_minute = 99;
uint8_t i = 0;
uint16_t t3,t4;
float vcc, hum, temp;
bool stateOfSwitch, lastStateOfSwitch = 99;


#ifdef MQTT
void connect2MQTT(){
  const char* host = MQTT_SERVER;
  Serial.print("Connecting to ");
  Serial.println(host);
  if (! espClient.connect(host, MQTT_PORT)) {
    Serial.println("Connection failed. Halting execution.");
    while(1);
  }
  if (espClient.verify(fingerprint, host)) {
    Serial.println("Connection secure.");
  } else {
    Serial.println("Connection insecure! Halting execution.");
    while(1);
  }
}
void publishData(){
  String buff;
  char valStr[5];
  Serial.println(F("Sending:"));
  // Publish temperature
  Serial.print("Tmp.: ");
  Serial.print(temp);
  Serial.print(" ...");
  buff =  (String)temp;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_TEMP, valStr))
  {
    Serial.println(F("Failed"));
  }
  else{Serial.println(F("OK!"));}

  // Publish humidity
  Serial.print("Hum.: ");
  Serial.print(hum);
  Serial.print(" ...");
  buff =  (String)hum;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_HUM, valStr))
  {
    Serial.println(F("Failed"));
  }
  else{Serial.println(F("OK!"));}

  // Publish voltage
  Serial.print("VCC: ");
  Serial.print(vcc);
  Serial.print(" ...");
  buff =  (String)vcc;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_VCC, valStr))
  {
    Serial.println(F("Failed"));
  }
  else{Serial.println(F("OK!"));}
  Serial.println(F(""));
}
void publishStatus(){
  Serial.print("Status: ");
  Serial.print(stateOfSwitch ? "ON" : "OFF");
  Serial.print(" ...");
  buff =  (String)(stateOfSwitch ? "ON" : "OFF") ;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_STATE, valStr))
  {
    Serial.println(F("Failed"));
  }
  else{Serial.println(F("OK!"));}
  //publish timer value
  Serial.print("Timer: ");
  Serial.print(timer_val);
  Serial.print(" ...");
  buff = (String)timer_val;
  buff.toCharArray(valStr,5);
  if(!client.publish(USER PREAMBLE F_TIMER, valStr))
  {
    Serial.println(F("Failed"));
  }
  else{Serial.println(F("OK!"));}

}
void callback(char* topic, byte* payload, unsigned int length){

  Serial.print("-> ");
  /*
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
  */
  //Serial.print("length: ");
  //Serial.println(length);

  //converting value to
  int val = 0;
  for (int i=0; i<length; i++){
    val = val*10 + (payload[i])-48;
    //Serial.println(val);
  }
  //val = atoi((char*)payload);

  Serial.println(val);

  if (val == 0){stateOfSwitch = 0;}
  else if (val > 0){stateOfSwitch = 1;}
  else{stateOfSwitch = 0;}
  timer_val = val;

}
void connect2MQTTBroker(){
  // Loop until we're reconnected
  if (!client.connected()) {
    Serial.print(F("\n."));
    // Attempt to connect
    if (client.connect("", USER, PASS, USER PREAMBLE F_CON, 1, 1, "OFFLINE")) {
      Serial.println(F("!"));
      // Once connected, publish an announcement...
      client.publish(USER PREAMBLE F_CON,"ONLINE");
      // ... and resubscribe
      client.subscribe(USER PREAMBLE F_TIMER,1);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
#endif

void readDHTAndPublishData() {
  // Wait at least 2 seconds seconds between measurements.
  // Reading temperature for humidity takes about 250 milliseconds!
  hum = dht.readHumidity();          // Read humidity (percent)
  temp = dht.readTemperature();     // Read temperature in Degrees Celsius
  // Check if any reads failed and exit early (to try again).
  if (isnan(hum) || isnan(temp))
  {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  // Read vcc
  vcc = ESP.getVcc();
  vcc = vcc / 1000;
  // publish data to MQTT broker
  #ifdef MQTT
  publishData();
  #endif
}

//function to prepend string with a zero if it's only single digit
String m2D(int s){
  String sReturn = "DD";
  if (s < 10){sReturn = ("0"+String(s));}
  else {sReturn = String(s);}
  return sReturn;
}

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
  sBuff = (h +" % r.F. "+ h); //why the second "h"??? -> probably not right
  sBuff.toCharArray(buf, len);
  LCDString(buf);
  // go to 5th row
  gotoXY(0, 4);
  // print timer value and status
  t = String(timer_val);
  // set String h to either "ON" or "OFF" depending on stateOfSwitch
  h = (stateOfSwitch ? "ON" : "OFF");
  sBuff = (t +" min - "+ h);
  sBuff.toCharArray(buf, len);
  LCDString(buf);

  gotoXY(0, 5);
  // print timer value and status
  t = String(ESP.getFreeHeap());
  // set String h to either "ON" or "OFF" depending on stateOfSwitch
  t.toCharArray(buf, len);
  LCDString(buf);

}

void setup_wifi(){
  //delay(100);
  #ifdef DEBUG
  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println();
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

#ifdef TELEGRAM
void handleNewMessages(int numNewMessages) {
  #ifdef DEBUG
  Serial.println(F("handleNewMessages"));
  Serial.println(String(numNewMessages));
  #endif
  for (int i=0; i<numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    String senderID = bot.messages[i].from_id;
    //if (from_name == "") from_name = "Guest";

    if (senderID == adminID) {
      Serial.println(text);
      if (text == "/start") {
        String welcome = "Hi " + from_name + ",\n";
        welcome += "Wie kann ich helfen?\n\n";
        welcome += "/on : zum Einschalten\n";
        welcome += "/off : zum Ausschalten\n";
        welcome += "/status : Anzeige des akt. Status\n";
        welcome += "/options : zeigt alle Optionen\n";
        bot.sendMessage(chat_id, welcome, "Markdown");
      }
      else if (text == "/options") {
        String keyboardJson = "[[\"/on\", \"/off\"],[\"/status\"]]";
        bot.sendMessageWithReplyKeyboard(chat_id, "Wähle eine der folgenden Optionen:", "", keyboardJson, true);
      }
      else if (text == "/on") {
        digitalWrite(SWITCHPIN, HIGH);   // turn the LED on (HIGH is the voltage level)
        stateOfSwitch = 1;
        timer_val = 30;
        bot.sendMessage(chat_id, "ist AN", "");
      }
      else if (text == "/off") {
        stateOfSwitch = 0;
        digitalWrite(SWITCHPIN, LOW);    // turn the LED off (LOW is the voltage level)
        bot.sendMessage(chat_id, "ist AUS", "");
      }
      else if (text == "/status") {
        String buff;
        buff = (String)temp;
        bot.sendMessage(chat_id, "Temperatur: " + buff + " °C","");
        buff = (String)hum;
        bot.sendMessage(chat_id, "Feuchte: " + buff + " % r.F.","");
        buff = (String)timer_val;
        bot.sendMessage(chat_id, "Timer: " + buff + " min","");
        if(stateOfSwitch){
          bot.sendMessage(chat_id, "ist AN", "");
        } else {
          bot.sendMessage(chat_id, "ist AUS", "");
        }
      }
      else {
        bot.sendMessage(chat_id, "?!?: " + text, "");
        //bot.sendPhoto(chat_id, "https://ih1.redbubble.net/image.31887377.4850/fc,550x550,white.jpg", from_name + ", please leave me alone");
      }
    }
    else {
      //bot.sendPhoto(chat_id, "CAADAgADSiYAAktqAwABBe5Xc6njuu8C", from_name + ", please leave me alone");
      bot.sendMessage(chat_id, "Zugriff verweigert. Deine ID: " + senderID, "");
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
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}
#endif

//function to detect change of minute, if so, return 1
//
bool minuteGone(){
  //check if minute changed
  uint16_t act_minute = minute();
  if (act_minute != last_minute){
    last_minute = act_minute;
    //return 1
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
void updateSwitch(){
  if (stateOfSwitch != lastStateOfSwitch){
    if (stateOfSwitch == 0){
      Serial.println(F("Sw. OFF"));
      digitalWrite(SWITCHPIN, LOW);
    }
    else if (stateOfSwitch == 1){
      Serial.println(F("Sw. ON"));
      digitalWrite(SWITCHPIN, HIGH);
    }
    lastStateOfSwitch = stateOfSwitch;

    #ifdef MQTT
    String buff;
    char valStr[5];
    buff = stateOfSwitch ? "ON" : "OFF";
    buff.toCharArray(valStr,5);

    if(!client.publish(USER PREAMBLE F_TEMP, valStr))
    {
      Serial.println(F("Failed"));
    } else {
      Serial.println(F("OK!"));
    }
    #endif

  }
}

void decrementTimerValueAndUpdateSwitchState(){
  //if timer =! 0, decrease timer value and set stateOfSwitch to 1 (=On)
  if (timer_val >= 1){
    timer_val--;
    stateOfSwitch = 1;
  }
  //if timer = 0, set stateOfswitch to 0 (=Off)
  else if (timer_val <= 0){
    stateOfSwitch = 0;
  }

}

void setup() {
  pinMode(SWITCHPIN, OUTPUT);
  LCDInit(); //Init the LCD
  LCDClear();
  delay(2000);

  Serial.begin(115200);
  Serial.println("Booting");

  Serial.printf("Flash size: %d Bytes \n", ESP.getFlashChipRealSize());

  setup_wifi();

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

  if (WiFi.status() == 6)
   {
     ESP.reset();
   }

  //sync output pin every second
  if(secondGone()){
    updateSwitch();
    updateLCD();
    #ifdef TELEGRAM
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while(numNewMessages) {
      Serial.println(F("Nachricht erhalten!"));
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    #endif
    seconds++;
  }
  //read DHT and vcc and publish to MQTT feeds every 10 s
  if(seconds >= 10){
    readDHTAndPublishData();
    seconds = 0;
  }
  //update timer value and switch state every minute
  if (minuteGone()){
    decrementTimerValueAndUpdateSwitchState();
    //Serial.println("*");
    #ifdef MQTT
    //publish status
    publishStatus();
    #endif
    Serial.println(timeClient.getFormattedTime());
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
