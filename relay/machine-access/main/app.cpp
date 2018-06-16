#include "main.h"
#include "kv.h"

#include <stdio.h>
#include <string.h>

/* Pin mappings for NodeMCU

static const uint8_t D0   = 16;
static const uint8_t D1   = 5;
static const uint8_t D2   = 4;
static const uint8_t D3   = 0;
static const uint8_t D4   = 2;
static const uint8_t D5   = 14;
static const uint8_t D6   = 12;
static const uint8_t D7   = 13;
static const uint8_t D8   = 15;
static const uint8_t D9   = 3;
static const uint8_t D10  = 1;

*/

enum class lock_state_t {
    LOCKED,
    UNLOCKED
};

static lock_state_t lock_state;

const struct app_deps *deps;

int app_init(struct app_deps *const deps)
{
    printf("%s: \n", __FUNCTION__);

    ::deps = deps;

    lock_state = lock_state_t::LOCKED;

    return 0;
}

int app_on_mqtt_connected()
{
    return deps->mqtt_publish("locked", lock_state == lock_state_t::LOCKED ? "1" : "0");
}

static void command_lock()
{
    printf("%s: \n", __FUNCTION__);

    if (lock_state == lock_state_t::LOCKED) {
        printf("Lock already locked.");
    } else {
        lock_state = lock_state_t::LOCKED;
    }

    deps->mqtt_publish("locked", "1");
}

static void command_unlock()
{
    printf("%s: \n", __FUNCTION__);

    if (lock_state == lock_state_t::UNLOCKED) {
        printf("Lock already unlocked.");
    } else {
        lock_state = lock_state_t::UNLOCKED;
    }

    deps->mqtt_publish("locked", "0");
}

enum class command_type {
    UNKNOWN,
    LOCK,
    UNLOCK,
    REFRESH,
};

struct command {
    command_type type;
    char request[10];
} command;

int on_item(void *, const char *key, const char *value)
{
    if (strcmp("command", key) == 0) {
        if (strcmp("lock", value) == 0) {
            command.type = command_type::LOCK;
        } else if (strcmp("lock", value) == 0) {
            command.type = command_type::UNLOCK;
        } else {
            deps->mqtt_publish("error", "unknown command");
            return 1;
        }

        return 0;
    } else if (strcmp("request", key) == 0) {
        int sz = strlcpy(command.request, value, sizeof(command.request));
        if (sz < sizeof(command.request)) {
            deps->mqtt_publish("error", "too long request field");
            return 1;
        }
    }

    deps->mqtt_publish("error", "Unknown key");
    return 1;
}

void app_on_command(MQTTMessage *msg)
{
    char kbuf[10], vbuf[20];
    command.type = command_type::UNKNOWN;
    struct kv_parser parser;
    int ret;

    ret = kv_parser_init(&parser, on_item, NULL, kbuf, sizeof(kbuf), vbuf, sizeof(vbuf));
    if (ret) {
        deps->mqtt_publish("error", "Could not init parser");
        return;
    }
    ret = kv_parser_add(&parser, (char *)msg->payload, msg->payloadlen);
    if (ret) {
        // Error signalled by on_item
        return;
    }

    if (command.type == command_type::UNKNOWN) {
    } else if (command.type == command_type::LOCK) {
        command_lock();
    } else if (command.type == command_type::UNKNOWN) {
        command_unlock();
    } else {
        deps->mqtt_publish("error", "Unknown command");
    }
}
