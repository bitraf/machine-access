#include <ESP8266WiFi.h>
#include "PubSubClient.h"

const String mqtt_topic_prefix = "bitraf/machineaccess";
const String mqtt_client_id = "machine_access_" + mqtt_topic_device_name;

const char *mqtt_broker = "mqtt.bitraf.no";

const String mqtt_topic_base =
    mqtt_topic_prefix + "/" + mqtt_topic_device_name + "/";

// subscriptions
const String mqtt_topic_lock = mqtt_topic_base + "lock";
const String mqtt_topic_unlock = mqtt_topic_base + "unlock";

// publications
const String mqtt_topic_error = mqtt_topic_base + "error";
const String mqtt_topic_is_locked = mqtt_topic_base + "is_locked";
const String mqtt_topic_is_running = mqtt_topic_base + "is_running";

const char led_pin = 2;
const char optocoupler_pin = 5;
const char relay_pin = 4;

#define RELAY_ON HIGH
#define RELAY_OFF LOW

#define LED_ON LOW
#define LED_OFF HIGH

WiFiClient wfClient;
PubSubClient psClient(wfClient);

bool the_machine_is_locked = false;
bool the_machine_is_running = false;

void lock_or_unlock_the_machine() {
  if (the_machine_is_locked) {
    digitalWrite(relay_pin, RELAY_OFF);
  } else {
    digitalWrite(relay_pin, RELAY_ON);
  }
}

void publish_whether_the_machine_is_locked() {
  if (the_machine_is_locked) {
    psClient.publish(mqtt_topic_is_locked.c_str(), "true");
  } else {
    psClient.publish(mqtt_topic_is_locked.c_str(), "false");
  }
}

void mqtt_callback(const char *topic_c_str, byte *payload, unsigned length) {
  const String topic(topic_c_str);

  if (mqtt_topic_lock == topic) {
    the_machine_is_locked = true;
    lock_or_unlock_the_machine();
    publish_whether_the_machine_is_locked();
    return;
  }

  if (mqtt_topic_unlock == topic) {
    the_machine_is_locked = false;
    lock_or_unlock_the_machine();
    publish_whether_the_machine_is_locked();
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
  Serial.println(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("connecting to broker ");
  Serial.println(mqtt_broker);

  psClient.setServer(mqtt_broker, 1883);
  psClient.setCallback(mqtt_callback);

  psClient.connect(mqtt_client_id.c_str());

  psClient.subscribe(mqtt_topic_lock.c_str());
  psClient.subscribe(mqtt_topic_unlock.c_str());
}

void loop() {
  if (!psClient.connected()) {
    Serial.println("not connected");
  }

  psClient.loop();

  {
    static long last_time = 0;
    static char led_state = LED_OFF;

    long now = millis();
    if (1000 < now - last_time) {
      last_time = now;
      led_state = LED_ON == led_state ? LED_OFF : LED_ON;
      digitalWrite(led_pin, led_state);
    }
  }
}
