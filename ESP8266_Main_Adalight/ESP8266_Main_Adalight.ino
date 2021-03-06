/*
 * To setup credentials and other settings create a "settings.h" file in root of this sketch with this code below:
  #define WIFI_SSID "My_Wi-Fi"
  #define WIFI_PASSWORD "password"
  
  #define MQTT_HOST IPAddress(192, 168, 1, 10)
  #define MQTT_PORT 1883
  #define MQTT_USER "mqttUSR"
  #define MQTT_PASS "mqttPASS"
  #define OTA_PASS "otaPASS"
 */

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "settings.h"
#include "serialFunc.h"

#define MQTT_QOS 0
#define heartbeatInterval 15000

unsigned long heartbeat_prevMillis = 0, currentMillis;

#define waitACKInterval 50
#define MAX_ACK_RETRY 20
unsigned long waitACK_prevMillis = 0;
String currentACK;
bool waitACK = false;
byte CUR_ACK_TRY = 0;

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.println("Subscribing...");
  mqttClient.subscribe("/adalight/statecmd", MQTT_QOS);
  mqttClient.subscribe("/adalight/mode", MQTT_QOS);
  mqttClient.subscribe("/adalight/brightness", MQTT_QOS);
  mqttClient.subscribe("/adalight/welcomemessage", MQTT_QOS);
  mqttClient.subscribe("/main_node/reboot", MQTT_QOS);
  mqttClient.subscribe("/main_node/reqstat", MQTT_QOS);
  mqttClient.subscribe("/adalight/acktest", MQTT_QOS);
  mqttClient.subscribe("/adalight/RGB", MQTT_QOS);
  
  char ipaddr[16];
  mqttClient.publish("/nodemcu/status", MQTT_QOS, false, "Connected");
      
  sprintf(ipaddr, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3] );
  
  mqttClient.publish("/nodemcu/ip", MQTT_QOS, false, ipaddr);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
}

void onMqttUnsubscribe(uint16_t packetId) {
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);

  String payloadstr;

  for (int i = 0; i < len; i++) 
  {
    payloadstr = String(payloadstr + (char)payload[i]);  //convert payload to string
  }
  Serial.println(payloadstr);

  CUR_ACK_TRY = 0;
  
  if(strcmp((char*)topic, "/adalight/statecmd") == 0)
  { 
    waitACK = true;
    if(payloadstr=="1") //On Adalight
    {
      mqttClient.publish("/adalight/state", MQTT_QOS, false, "1");
      Serial.print("<state, 1>");
      currentACK = "<state, 1>";
    }
    else if(payloadstr=="0") //Off Adalight
    {
      mqttClient.publish("/adalight/state", MQTT_QOS, false, "0");
      Serial.print("<state, 0>");
      currentACK = "<state, 0>";
    }
  }

  if(strcmp((char*)topic, "/adalight/mode") == 0)
  {
    Serial.print("<mode, " + payloadstr + ">");
    currentACK = "<mode, " + payloadstr + ">";
    waitACK = true;
  }
  
  if(strcmp((char*)topic, "/adalight/brightness") == 0)
  { 
    Serial.print("<brightness, " + payloadstr + ">");
    currentACK = "/adalight/brightness";
    waitACK = true;
  }

  if(strcmp((char*)topic, "/adalight/RGB") == 0)
  {
    Serial.print("<ledRGB, " + payloadstr + ">");
    currentACK = "<ledRGB, " + payloadstr + ">";
    waitACK = true;
  }
  
 if(strcmp((char*)topic, "/adalight/welcomemessage") == 0)
  { 
    Serial.print("<welcomemsg, 55>");
    currentACK = "<welcomemsg, 55>";
    waitACK = true;
  }
  //ACKTEST
  if(strcmp((char*)topic, "/adalight/acktest") == 0)
  { 
    Serial.println("BEGIN ACK TEST");
    waitACK = true;
  }

  if(strcmp((char*)topic, "/main_node/reboot") == 0) //exposes reboot function
  {
    ESP.restart();
  }

  if(strcmp((char*)topic, "/main_node/reqstat") == 0)  // Request statistics function
  {
    unsigned long REQ_STAT_CUR_MILLIS = millis(); // gets current millis

    char REQ_STAT_CUR_TEMPCHAR[60];

    snprintf(
      REQ_STAT_CUR_TEMPCHAR,
      60, 
      "%d.%d.%d.%d,%lu", 
      WiFi.localIP()[0], 
      WiFi.localIP()[1],
      WiFi.localIP()[2], 
      WiFi.localIP()[3],
      (int)REQ_STAT_CUR_MILLIS
    );  // convert string to char array for Millis. Elegance courtesy of Shahmi Technosparks

    mqttClient.publish("/main_node/curstat", MQTT_QOS, false, REQ_STAT_CUR_TEMPCHAR); //publish to topic and tempchar as payload
  }
}

void onMqttPublish(uint16_t packetId) {
}

void mqttSetup()
{
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
}

void otaSetup()
{
  while (WiFi.status() != WL_CONNECTED) //wait for wifi to connect before ota setup
  {
    delay(100);
    Serial.print(".");
  }
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("MainNodeMCU");

  // No authentication by default
  ArduinoOTA.setPassword(OTA_PASS);

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
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
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  
  WiFi.mode(WIFI_STA);
  mqttSetup();
  connectToWifi();
  otaSetup();
}

void loop() { 
  ArduinoOTA.handle();
  
  recvWithStartEndMarkers();
  if (newData == true) 
  {
    strcpy(tempChars, receivedChars);
    parseData();
    
    //serialDebug();  //uncomment to enable serial input debug
    
    //ACK
    if(strcmp (messageRecv,"ack") == 0)
    { 
      Serial.println("ACK received!");
      currentACK = "NuLL";
      waitACK = false;
    }
  
    newData = false;  //finishes serial data parsing
  }

  if(waitACK==true)
  {
    if(currentMillis - waitACK_prevMillis >= waitACKInterval){
      if(CUR_ACK_TRY < MAX_ACK_RETRY)
      {
      waitACK_prevMillis = currentMillis;
      Serial.println("noACK! Resending...");
      Serial.println(currentACK);
      //Serial.print("CUR_ACK_TRY:");
      //Serial.println(CUR_ACK_TRY);
      CUR_ACK_TRY++;
      }
      else
      {
        waitACK = false;
        CUR_ACK_TRY = 0;
        Serial.println("Retry ACK timeout");
      }
    }
  }
  
  currentMillis = millis();
  if (currentMillis - heartbeat_prevMillis >= heartbeatInterval) 
  {
    heartbeat_prevMillis = currentMillis;
    mqttClient.publish("/nodemcu/heartbeat", MQTT_QOS, false, "Hi");
  }
  
  if(currentMillis> 4094967296) //reboot on overflow
  {
    ESP.restart();
  }
}
