#include "main.h"
#include "kv.h"

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

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

struct {
    bool locked;
    int pin;
    char pattern[100];
} locks[CONFIG_MAIN_MA_LOCK_COUNT];

static void on_lock_command(MessageData *msg, int i);

#if CONFIG_MAIN_MA_LOCK_COUNT > 0
void on_lock_command_0(MessageData* msg)
{
    on_lock_command(msg, 0);
}
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 1
void on_lock_command_1(MessageData* msg)
{
    on_lock_command(msg, 1);
}
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 2
void on_lock_command_2(MessageData* msg)
{
    on_lock_command(msg, 2);
}
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 3
void on_lock_command_3(MessageData* msg)
{
    on_lock_command(msg, 3);
}
#endif

static const struct app_deps *deps;

int app_init(struct app_deps *const deps)
{
    ::deps = deps;

    printf("%s: There are %d locks configured\n", __FUNCTION__, CONFIG_MAIN_MA_LOCK_COUNT);

#if CONFIG_MAIN_MA_LOCK_COUNT > 0
    locks[0].pin = CONFIG_MAIN_MA_LOCK_1_PIN;
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 1
    locks[1].pin = CONFIG_MAIN_MA_LOCK_2_PIN;
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 2
    locks[2].pin = CONFIG_MAIN_MA_LOCK_3_PIN;
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 3
    locks[3].pin = CONFIG_MAIN_MA_LOCK_4_PIN;
#endif

    for (int i = 0; i < CONFIG_MAIN_MA_LOCK_COUNT; i++) {
        locks[i].locked = 1;
        char buf[sizeof(locks[0].pattern)];

        if (snprintf(buf, sizeof(buf), "%d/command", i) >= sizeof(buf)) {
            return 1;
        }

        int ret = deps->mqtt_format(locks[i].pattern, sizeof(locks[i].pattern), buf);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

static void command_lock(int lock_id, int state)
{
    printf("%s: lock_id=%d\n", __FUNCTION__, lock_id);

    if (state && locks[lock_id].locked) {
        printf("%s: Lock already locked.\n", __FUNCTION__);
    } else if (!state && !locks[lock_id].locked) {
        printf("%s: Lock already unlocked.\n", __FUNCTION__);
    } else {
        locks[lock_id].locked = state;
    }

    char buf[10];
    int sz;

    sz = snprintf(buf, sizeof(buf), "%d/locked", lock_id);
    if (sz >= sizeof(buf)) {
        return;
    }
    deps->mqtt_publish(buf, state ? "1" : "0");
}

static int command_refresh()
{
    for (int i = 0; i < CONFIG_MAIN_MA_LOCK_COUNT; i++) {
        char buf[10];
        int sz;

        sz = snprintf(buf, sizeof(buf), "%d/locked", i);
        if (sz >= sizeof(buf)) {
            return ENOMEM;
        }

        int fail = deps->mqtt_publish(buf, locks[i].locked ? "1" : "0");
        if (fail) {
            return ENOMEM;
        }
    }

    return 0;
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

static int app_on_command_on_item(void *, const char *key, const char *value)
{
    if (strcmp("command", key) == 0) {
        if (strcmp("lock", value) == 0) {
            command.type = command_type::LOCK;
        } else if (strcmp("unlock", value) == 0) {
            command.type = command_type::UNLOCK;
        } else if (strcmp("refresh", value) == 0) {
            command.type = command_type::REFRESH;
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
    struct kv_parser parser;
    int ret;

    command.type = command_type::UNKNOWN;

    ret = kv_parser_init(&parser, app_on_command_on_item, NULL, kbuf, sizeof(kbuf), vbuf, sizeof(vbuf));
    if (ret) {
        deps->mqtt_publish("error", "Could not init parser");
        return;
    }
    ret = kv_parser_add(&parser, (char *)msg->payload, msg->payloadlen);
    if (ret) {
        printf("Could not parse chunk\n");
        // Error signalled by command_on_item
        return;
    } else {
        ret = kv_parser_end(&parser);
    }

    if (command.type == command_type::UNKNOWN) {
    } else if (command.type == command_type::REFRESH) {
        command_refresh();
    } else {
        deps->mqtt_publish("error", "Unknown command");
    }
}

static int on_lock_command_on_item(void *, const char *key, const char *value)
{
    if (strcmp("command", key) == 0) {
        if (strcmp("lock", value) == 0) {
            command.type = command_type::LOCK;
        } else if (strcmp("unlock", value) == 0) {
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

static void on_lock_command(MessageData *data, int lock_id)
{
    char kbuf[10], vbuf[20];
    struct kv_parser parser;
    int ret;
    MQTTMessage *msg = data->message;

    command.type = command_type::UNKNOWN;

    printf("%s: payloadlen=%d\n", __FUNCTION__, msg->payloadlen);

    ret = kv_parser_init(&parser, on_lock_command_on_item, NULL, kbuf, sizeof(kbuf), vbuf, sizeof(vbuf));
    printf("%s: kv_parser_init=%d\n", __FUNCTION__, ret);
    if (ret) {
        deps->mqtt_publish("error", "Could not init parser");
        return;
    }
    ret = kv_parser_add(&parser, (char *)msg->payload, msg->payloadlen);
    printf("%s: kv_parser_add=%d\n", __FUNCTION__, ret);
    if (ret) {
        printf("Could not parse chunk\n");
        // Error signalled by command_on_item
        return;
    } else {
        ret = kv_parser_end(&parser);
    }

    printf("%s: command.type=%d\n", __FUNCTION__, command.type);

    if (command.type == command_type::UNKNOWN) {
    } else if (command.type == command_type::LOCK) {
        command_lock(lock_id, 1);
    } else if (command.type == command_type::UNLOCK) {
        command_lock(lock_id, 0);
    } else {
        deps->mqtt_publish("error", "Unknown command");
    }
}

int app_on_mqtt_connected()
{
#if CONFIG_MAIN_MA_LOCK_COUNT > 0
    deps->mqtt_subscribe(locks[0].pattern, on_lock_command_0);
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 1
    deps->mqtt_subscribe(locks[1].pattern, on_lock_command_1);
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 2
    deps->mqtt_subscribe(locks[2].pattern, on_lock_command_2);
#endif
#if CONFIG_MAIN_MA_LOCK_COUNT > 3
    deps->mqtt_subscribe(locks[3].pattern, on_lock_command_3);
#endif
    return command_refresh();
}
