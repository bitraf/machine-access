#include <ESP8266WiFi.h>
#include "PubSubClient.h"
 
const char* ssid     = "ssid";
const char* password = "password";
 
const char* broker = "mqtt.bitraf.no";

const char led_pin = 2;
const char optocoupler_pin = 5;
const char relay_pin = 4;

#define RELAY_ON HIGH
#define RELAY_OFF LOW

#define LED_ON LOW
#define LED_OFF HIGH

WiFiClient wfClient;
PubSubClient psClient(wfClient);

long lastTime;

void mqttCallback(const char *topic, byte *payload, unsigned length) {
  String topicString(topic);
  if (topicString != "/public/relay") {
    Serial.println("topic is not /public/relay");
    return;
  }

  if (2 == length && 'o' <= payload[0] && 'n' == payload[1]) {
    digitalWrite(relay_pin, RELAY_ON);
    return;
  }


  if (3 == length && 'o' <= payload[0] && 'f' == payload[1] && 'f' == payload[2]) {
    digitalWrite(relay_pin, RELAY_OFF);
    return;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  digitalWrite(relay_pin, RELAY_OFF);
  digitalWrite(led_pin, LED_OFF);

  pinMode(led_pin, OUTPUT);
  pinMode(relay_pin, OUTPUT);

  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("connecting to broker ");
  Serial.println(broker);

  psClient.setServer(broker, 1883);
  psClient.setCallback(mqttCallback);

  psClient.connect("relay");
  psClient.subscribe("/public/relay");
}

void loop() {
  //delay(1000);

  if (!psClient.connected()) {
    Serial.println("not connected");
  }

  psClient.loop();

  {
    static long last_time = 0;
    static char led_state = LED_OFF;

    long now = millis();
    if (1000 < now - lastTime) {
      lastTime = now;
      led_state = LED_ON == led_state ? LED_OFF : LED_ON;
      digitalWrite(led_pin, led_state);
    }
  }
}
