#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <arduino_secrets.h>
#include <WiFiUdp.h>

const char* ssid 		      = SECRET_GENERAL_WIFI_SSID;
const char* password 		  = SECRET_GENERAL_WIFI_PASSWORD;
const char* mqttServer	  = SECRET_MQTT_SERVER;
const int   mqttPort 		  = SECRET_MQTT_PORT;
const char* mqttUser 		  = SECRET_MQTT_USER;
const char* mqttPassword 	= SECRET_MQTT_PASSWORD;

// ==============================================================================
// S8 Init zone
// ==============================================================================

#define D7 (13)
#define D8 (15)
#define CO2_INTERVAL 15000

SoftwareSerial s8Serial(D7, D8);

int co2 = 0;
int co2_mean = 0;
int co2_mean2 = 0;
int abc_time = 0;
int s8_status = 0;

float smoothing_factor = 0.5;
float smoothing_factor2 = 0.15;

byte get_co2_cmd[]      = { 0xFE, 0x04, 0x00, 0x03, 0x00, 0x01, 0xD5, 0xC5 }; // Get CO2 value from sensor
byte get_abc_cmd[]      = { 0xFE, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xA1, 0xC3 }; // Get Auto Baseline Calibration time
byte get_stat_cmd[]     = { 0xFE, 0x04, 0x00, 0x00, 0x00, 0x01, 0x25, 0xC5 }; // Get sensor status (see: http://www.co2meters.com/Documentation/Datasheets/DS-S8-3.2.pdf for all codes)
byte get_co2_stat_cmd[] = { 0xFE, 0x04, 0x00, 0x00, 0x00, 0x04, 0xE5, 0xC6 }; // Get CO2 and sensor status in one replay
byte set_abc_off[]      = { 0xFE, 0x06, 0x00, 0x1F, 0x00, 0x00, 0xAC, 0x03 }; // Turn off abc
byte set_abc_on[]       = { 0xFE, 0x06, 0x00, 0x1F, 0x00, 0xB4, 0xAC, 0x74 }; // Turn on abc and set time to 180 hours

#define GET_CO2_RLEN 7
#define GET_STATUS_RLEN 7
#define GET_TWO_RLEN 13
#define GET_ABC_RLEN 7
#define SET_ABC_RLEN 8
#define BG_CALIBRATION_RLEN 8

#define GET_CO2_FLAG 1
#define GET_ABC_FLAG 2
#define GET_TWO_FLAG 3
#define SET_ABC_FLAG 4
#define BG_CALIBRATION_FLAG 8 

unsigned long lastCo2Measured = 0;


// ==============================================================================
// End S8 init zone
// ==============================================================================

WiFiClient    espClient;
PubSubClient  client(espClient);

WiFiUDP       wUDP;
const int     UDP_PORT = 911;
char          udp_packet[255];

char hostname[]           = "franki";

char mqtt_topic_status[]  = "esp/status/franki";
char mqtt_topic_data[]    = "esp/sensors/co2/franki";
char mqtt_topic_set[]     = "esp/set/franki";

unsigned long lastReconnectAttempt = 0;

const size_t capacity = JSON_OBJECT_SIZE(10) + 256;
StaticJsonDocument<capacity> jdoc;

boolean wifi_reconnect() {
  WiFi.hostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println();
  Serial.println();
  Serial.print("Connecting Wi-FI: ");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("WI-FI connected");

  jdoc["IP:"] = WiFi.localIP();

  wUDP.begin(UDP_PORT);

  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(hostname);
  // ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

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
  return true;
}

boolean mqtt_reconnect() {
  Serial.print("Connecting to MQTT...");
  if(client.connect(hostname, mqttUser, mqttPassword, mqtt_topic_status, 2, true, "offline")) {
    // Online Message
    client.publish(mqtt_topic_status, "online", true);
    client.subscribe(mqtt_topic_set);
    Serial.println("MQTT Connected");
  } else {
    Serial.printf("failed with state: %d\n", client.state());
  }
  return client.connected();
}

bool s8Request(byte cmd[], int8_t response_lenght, int8_t rFlag) {
  s8Serial.begin(9600);
  byte response[response_lenght];

  while(!s8Serial.available()) {
    s8Serial.write(cmd, 8);
    delay(50);
  }

  int timeout=0;
  while(s8Serial.available() < response_lenght ) {
    timeout++;
    if(timeout > 10) {
      while(s8Serial.available()) {
        s8Serial.read(); 
        break;
      }
    } 
    delay(50); 
  } 
  for (int i=0; i < response_lenght; i++) { 
    response[i] = s8Serial.read();
  }
  s8Serial.end();

  if(rFlag == GET_CO2_FLAG) {
    co2 = 0;
    int high = response[3];
    int low  = response[4];
    co2 = high*256 + low;

    if (!co2_mean) co2_mean = co2;
      co2_mean = co2_mean - smoothing_factor*(co2_mean - co2);
  
    if (!co2_mean2) co2_mean2 = co2;
      co2_mean2 = co2_mean2 - smoothing_factor2*(co2_mean2 - co2);

    jdoc["current"] = co2;
    jdoc["mean"] = co2_mean;
    jdoc["mean2"] = co2_mean2;

  }

  if(rFlag == GET_ABC_FLAG) {
    abc_time = 0;
    int high = response[3];
    int low  = response[4];
    abc_time = high*256 + low;
    jdoc["abc"] = abc_time;
  }

  if(rFlag == SET_ABC_FLAG) {
    if(memcmp(cmd, response, sizeof(cmd)) == 0) {
      return true;
    } 
    else {
      return false;
    }
  }

  if(rFlag == GET_TWO_FLAG) {
    int stat_high = response[4];
    //int stat_low  = response[5];
    int co2_high  = response[9];
    int co2_low   = response[10];

    s8_status = 0;
    co2 = 0;

    s8_status = stat_high;
    co2 = co2_high*256 + co2_low;

    if (!co2_mean) co2_mean = co2;
      co2_mean = co2_mean - smoothing_factor*(co2_mean - co2);
  
    if (!co2_mean2) co2_mean2 = co2;
      co2_mean2 = co2_mean2 - smoothing_factor2*(co2_mean2 - co2);

    jdoc["current"] = co2;
    jdoc["mean"] = co2_mean;
    jdoc["mean2"] = co2_mean2;
    jdoc["status"] = s8_status;

  }
  return true;
}    

void bg_calibration() {
  byte step_one[] = { 0xFE, 0x06, 0x00, 0x00, 0x00, 0x00, 0x9D, 0xC5 };
  byte step_two[] = { 0xFE, 0x06, 0x00, 0x01, 0x7C, 0x06, 0x6C, 0xC7 };

  s8Request(step_one, BG_CALIBRATION_RLEN, BG_CALIBRATION_FLAG);
  delay(2000);
  s8Request(step_two, BG_CALIBRATION_RLEN, BG_CALIBRATION_FLAG);
  delay(3000);

}

void HumanReadableTime() {
  unsigned long currentMillis;
  unsigned long seconds;
  unsigned long minutes;
  unsigned long hours;
  unsigned long days;

  currentMillis = millis();
  seconds = currentMillis / 1000;
  minutes = seconds / 60;
  hours = minutes / 60;
  days = hours / 24;
  currentMillis %= 1000;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;

  char timeBuffer [25];
  sprintf (timeBuffer, "%02d:%02d:%02d:%02d", days, hours, minutes, seconds);
  jdoc["Uptime"] = timeBuffer;
  memset(timeBuffer, 0, sizeof(timeBuffer));
}

void callback(char* topic, byte* payload, unsigned int length) {
  char buff_p[length];

  for (size_t i = 0; i < length; i++)
  {
    buff_p[i] = (char)payload[i];
  }
  buff_p[length] = '\0';

  if(strcmp(buff_p, "calibrate") == 0) {
    bg_calibration();
  }
}

void setup() {
  Serial.begin(115200);
  if(WiFi.status() != WL_CONNECTED) {
    wifi_reconnect();
  }

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
  mqtt_reconnect();

  s8Request(get_abc_cmd, GET_ABC_RLEN, GET_ABC_FLAG);
}

void loop() {

  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) { wifi_reconnect(); }

  if(!client.connected()) {
    unsigned long now = millis();
    if(now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if(mqtt_reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    client.loop();
  }

  int packetSize = wUDP.parsePacket();
  if (packetSize) {
    int len = wUDP.read(udp_packet, 255);
    if (len > 0)
    {
      udp_packet[len] = '\0';
    }

    if(strcmp(udp_packet, "reboot") == 0) {
      ESP.restart();
    }
  }

  unsigned long co2_time = millis();
  if(co2_time - lastCo2Measured > CO2_INTERVAL) {
    HumanReadableTime();
    s8Request(get_co2_stat_cmd, GET_TWO_RLEN, GET_TWO_FLAG);

    jdoc["JsonMemUsage"] = jdoc.memoryUsage();
    jdoc["freeHeap"] = ESP.getFreeHeap();

    char jsonBuffer[256];
    memset(jsonBuffer, 0, sizeof(jsonBuffer));
    size_t n = serializeJson(jdoc, jsonBuffer);
    client.publish(mqtt_topic_data, jsonBuffer, n);
    lastCo2Measured = co2_time;
    jdoc.garbageCollect();
  }


}