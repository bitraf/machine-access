#include <ESP8266WiFi.h>
#include "PubSubClient.h"
 
const char* ssid     = "ssid";
const char* password = "password";
 
const char* broker = "mqtt.bitraf.no";

const char relay_pin = 2;

WiFiClient wfClient;
PubSubClient psClient(wfClient);

long lastTime;

void mqttCallback(const char *topic, byte *payload, unsigned length) {
  String topicString(topic);
  if (topicString != "/public/relay") {
    Serial.println("topic is not /public/relay");
    return;
  }

  const unsigned pin = 2;

  digitalWrite(relay_pin, HIGH);
  delay(500);
  digitalWrite(relay_pin, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(100);

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
}
