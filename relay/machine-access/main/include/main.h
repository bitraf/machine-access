#pragma once

#include "MQTTClient.h"

struct app_deps {
    int (*mqtt_publish)(const char *topic, const char *payload);
    int (*mqtt_subscribe)(const char *pattern, void (*callback)(MessageData *));
    int (*mqtt_format)(char* buf, int sz, const char *pattern);
};

int app_init(struct app_deps *);
int app_on_mqtt_connected();
void app_on_command(MQTTMessage *);
