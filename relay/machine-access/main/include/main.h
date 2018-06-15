#pragma once

#include "MQTTClient.h"

struct app_deps {
    int (*mqtt_publish)(const char *topic, const char *payload);
};

int app_init(struct app_deps *);
int app_on_mqtt_connected();
void app_on_lock(MQTTMessage *);
void app_on_unlock(MQTTMessage *);
