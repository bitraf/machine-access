#include "esp_misc.h"
#include "esp_sta.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "spiffs.h"
#include "MQTTClient.h"
#include "timers.h"

#include "sdkconfig.h"
#include "main.h"
#include "main-priv.h"

#include "kv.h"

#include <assert.h>
#include <errno.h>
#include <ctype.h>

enum class topic_kind {
    DEVICE,
    MACHINE_ACCESS,
};

int mqtt_publish(topic_kind kind, const char *topic, const char *value);
int buf_to_cstr(char *dst, const int dst_sz, const void *src, const int src_sz);
int mqtt_string_to_cstr(char *buf, int sz, MQTTString *str);

int mqtt_queue_publish(topic_kind kind, const char *topic, const char *value);

struct main_config {
    char wifi_ssid[21] = CFG_WIFI_SSID;
    char wifi_password[21] = CFG_WIFI_PASSWORD;

    char mqtt_host[21] = CFG_MQTT_HOST;
    int mqtt_port = CFG_MQTT_PORT;
    char mqtt_client_id[21] = {0};

    char device_name[21] = {0};
};

struct main_config main_config;

enum events {
    EVENTS_GOT_IP = 1 << 0,
    EVENTS_LOST_IP = 1 << 1,
    EVENTS_CONNECT_MQTT = 1 << 2,
};

xTaskHandle main_task_handle = 0;

struct _reent fs_reent;

static const char *CONFIG_ITEM_WIFI_SSID = "wifi-ssid";
static const char *CONFIG_ITEM_WIFI_PASSWORD = "wifi-password";
static const char *CONFIG_ITEM_DEVICE_NAME = "device-name";

static int config_on_item(void *ctx, const char *key, const char *value)
{
    printf("%s: '%s'='%s'\n", __FUNCTION__, key, value);
    if (strcmp(CONFIG_ITEM_WIFI_SSID, key) == 0) {
        strncpy(main_config.wifi_ssid, value, sizeof(main_config.wifi_ssid));
    } else if (strcmp(CONFIG_ITEM_WIFI_PASSWORD, key) == 0) {
        strncpy(main_config.wifi_password, value, sizeof(main_config.wifi_password));
    } else if (strcmp(CONFIG_ITEM_DEVICE_NAME, key) == 0) {
        strncpy(main_config.device_name, value, sizeof(main_config.device_name));
    }
    return 0;
}

static void config_make_default()
{
    strncpy(main_config.wifi_ssid, CFG_WIFI_SSID, sizeof(main_config.wifi_ssid));
    strncpy(main_config.wifi_password, CFG_WIFI_PASSWORD, sizeof(main_config.wifi_password));

    strncpy(main_config.mqtt_host, CFG_MQTT_HOST, sizeof(main_config.mqtt_host));
    main_config.mqtt_port = CFG_MQTT_PORT;
    main_config.mqtt_client_id[0] = 0;

    main_config.device_name[0] = 0;
}

/**
 * Returns 0 on success. Check fs_reent._errno on failure.
 */
static int config_store()
{
    int fd = _spiffs_open_r(&fs_reent, "config.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd == -1) {
        return 1;
    }

    static const char * const items[][2] = {
        {CONFIG_ITEM_WIFI_SSID, main_config.wifi_ssid},
        {CONFIG_ITEM_WIFI_PASSWORD, main_config.wifi_password},
        {CONFIG_ITEM_DEVICE_NAME, main_config.device_name},
    };

    for (int i = 0; i < (sizeof(items) / sizeof(items[0])); i++) {
        const char* key = items[i][0];
        const char* value = items[i][1];
        char buf[100];
        int sz;

        // printf("key='%s', value='%s'\n", key, value);

        sz = kv_write_str(buf, sizeof(buf), key, value);
        if (sz == 0) {
            goto fail;
        }

        if (_spiffs_write_r(&fs_reent, fd, buf, sz) != sz) {
            goto fail;
        }
    }

    if (_spiffs_close_r(&fs_reent, fd) == -1) {
        return 1;
    }

    // SPIFFS doesn't support POSIX semantics for rename: 
    // https://github.com/pellepl/spiffs/issues/125
    if (_spiffs_unlink_r(&fs_reent, "config") == -1) {
        printf("%s: unlink failed\n", __FUNCTION__);
        return 1;
    }

    if (_spiffs_rename_r(&fs_reent, "config.tmp", "config") == -1) {
        printf("%s: rename failed\n", __FUNCTION__);
        return 1;
    }

    printf("%s: config written\n", __FUNCTION__);

    return 0;

fail:
    _spiffs_close_r(&fs_reent, fd);

    return 1;
}

static int config_load(char *buf, int sz)
{
    int ret;
    char kbuf[20];
    char vbuf[100];
    int read;
    struct kv_parser parser;

    // _spiffs_unlink_r(&fs_reent, "config");
    int fd = _spiffs_open_r(&fs_reent, "config", O_RDONLY, 0);

    if (fd < 0) {
        if (fs_reent._errno != ENOENT) {
            printf("Could not open config file: errno=%d, spiffs errno=%d\n", fs_reent._errno, _spiffs_fs_errno());
            return ENOENT;
        }
        printf("No config file found, creating default configuration\n");
        config_make_default();
        if (config_store()) {
            printf("Failed to write new config, errno=%d, spiffs=%d\n", fs_reent._errno, _spiffs_fs_errno());
            return 1;
        }
        return 0;
    }

    ret = kv_parser_init(&parser, config_on_item, NULL, kbuf, sizeof(kbuf), vbuf, sizeof(vbuf));
    if (ret) {
        goto fail;
    }

    while ((read = _spiffs_read_r(&fs_reent, fd, buf, sz)) > 0) {
        ret = kv_parser_add(&parser, buf, read);
        if (ret) {
            goto fail;
        }
    }
    if (read == -1) {
        goto fail;
    }
    ret = kv_parser_end(&parser);

fail:
    _spiffs_close_r(&fs_reent, fd);
    return ret;
}

__attribute__((unused))
static const char *failok(int ret)
{
    return ret ? "FAIL" : "OK";
}

void wifi_event_handler_cb(System_Event_t *event)
{
    if (event == NULL) {
        os_printf("NULL event\n");
        return;
    }

    if (event->event_id == EVENT_STAMODE_GOT_IP) {
        // printf("STA GOT IP\n");
        if (main_task_handle) {
            xTaskNotify(main_task_handle, EVENTS_GOT_IP, eSetBits);
        }
    } else if (event->event_id == EVENT_STAMODE_CONNECTED) {
        os_printf("STA CONNECTED\n");
    } else if (event->event_id == EVENT_STAMODE_DISCONNECTED) {
        if (main_task_handle) {
            xTaskNotify(main_task_handle, EVENTS_LOST_IP, eSetBits);
        }
        wifi_station_connect();
    } else {
        os_printf("unhandled event=%d\n", event->event_id);
    }
}

static void on_device_name(MessageData *data)
{
    const char *err;

    const int len = data->message->payloadlen;
    if (len >= sizeof(main_config.device_name)) {
        err = "too long device name";
        goto bad_input;
    }

    char tmp[len + 1];

    for (int i = 0; i < len; i++) {
        char c = ((char *)data->message->payload)[i];

        int ok = isalnum(c) || c == '_' || c == '-';

        if (!ok) {
            auto err = "bad device name, must be one of [a-zA-Z0-9-_]";
            printf("%s: %s\n", __FUNCTION__, err);
            mqtt_queue_publish(topic_kind::DEVICE, "error", err);
            goto bad_input;
        }

        tmp[i] = c;
    }

    tmp[len] = 0;

    // We subscribe to our own /name topic, don't start a name
    // changing loop.
    if (strcmp(main_config.device_name, tmp) == 0) {
        return;
    }

    strcpy(main_config.device_name, tmp);
    main_config.device_name[len] = 0;

    printf("%s: new device name: '%s'\n", __FUNCTION__, main_config.device_name);

    config_store();

    return;

bad_input:
    printf("%s: %s\n", __FUNCTION__, err);
    mqtt_queue_publish(topic_kind::DEVICE, "error", err);
    mqtt_queue_publish(topic_kind::DEVICE, "name", main_config.device_name);
}

static void on_app_message(MessageData *data)
{
    app_on_command(data->message);
}

// ------------------------------------------------------------
// MQTT
//

static int format_topic(char *buf, size_t sz, topic_kind kind, const char *suffix)
{
    uint32_t chip_id = system_get_chip_id();
    int count = snprintf(buf, sz, CONFIG_MAIN_MQTT_PREFIX "/%s/esp-%02x%02x%02x/%s",
                         kind == topic_kind::DEVICE ? "device" : "machine-access",
                         (chip_id >> 16) & 0xff, (chip_id >> 8) & 0xff, chip_id & 0xff, suffix);
    return count < sz ? 0 : ENOMEM;
}

int buf_to_cstr(char *dst, const int dst_sz, const void *src, const int src_sz)
{
    if (src_sz > dst_sz - 1) {
        return 0;
    }
    memmove(dst, src, src_sz);
    dst[src_sz] = 0;
    return src_sz;
}

int mqtt_string_to_cstr(char *buf, int sz, MQTTString *str)
{
    if (str->cstring) {
        int tmp = snprintf(buf, sz, "%s", str->cstring);
        if (tmp >= sz) {
            buf[0] = 0;
            return -ENOMEM;
        }

        return sz;
    } else {
        if (sz - 1 < str->lenstring.len) {
            return -ENOMEM;
        }
        memmove(buf, str->lenstring.data, str->lenstring.len);
        buf[str->lenstring.len] = 0;
        return str->lenstring.len;
    }
}

static MQTTClient mqtt_client;
static Network mqtt_network;
unsigned char mqtt_tx_buf[1000], mqtt_rx_buf[1000];
TimerHandle_t mqtt_connect_wait_timer;
int mqtt_do_connect;

// Topics that we subscribe to can't be stack allocated
char topic_device_name[100];
char topic_command[100];

static int mqtt_connect()
{
    int ret;
    char buf[100];
    // static int count = 0;

    if (0) {
        int ms = 1000;
        printf("pre mqtt_connect(): waiting for %d ms\n", ms);
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    ret = format_topic(buf, sizeof(buf), topic_kind::DEVICE, "online");
    if (ret) {
        return ret;
    }

    if ((ret = NetworkConnect(&mqtt_network, main_config.mqtt_host, main_config.mqtt_port)) != 0) {
        printf("%s: NetworkConnect:%d\n", __FUNCTION__, ret);
        return ret;
    }
    if (1) {
        printf("%s: network connected\n", __FUNCTION__);
    }

    MQTTPacket_connectData cd = MQTTPacket_connectData_initializer;
    cd.keepAliveInterval = 15;
    cd.willFlag = 1;
    cd.will.retained = 1;
    cd.will.topicName = MQTTString_initializer;
    cd.will.topicName.cstring = buf;
    cd.will.message = MQTTString_initializer;
    cd.will.message.cstring = (char *)"0";
    cd.MQTTVersion = 4;
    cd.clientID.cstring = main_config.mqtt_client_id;

    if ((ret = MQTTConnect(&mqtt_client, &cd)) != 0) {
        printf("%s: MQTTConnect:%d\n", __FUNCTION__, ret);
        return ret;
    }
    if (1) {
        printf("%s: mqtt connected\n", __FUNCTION__);
    }

#if defined(MQTT_TASK)
    if ((ret = MQTTStartTask(&mqtt_client)) != pdPASS) {
        printf("%s: MQTTStartTask:%d\n", __FUNCTION__, ret);
        return ret;
    }
#endif

    ret = MQTTSubscribe(&mqtt_client, topic_device_name, QOS1, on_device_name);
    if (ret) {
        return ret;
    }

    ret = MQTTSubscribe(&mqtt_client, topic_command, QOS1, on_app_message);
    if (ret) {
        return ret;
    }

    ret = mqtt_publish(topic_kind::DEVICE, "online", "1");
    if (ret) {
        return ret;
    }

    int sz = snprintf(buf, sizeof(buf), "build-rev: %s\nbuild-timestamp: %s", MAIN_GIT_REV, __TIMESTAMP__);

    if (sz >= sizeof(buf)) {
        return ENOMEM;
    }

    ret = mqtt_publish(topic_kind::DEVICE, "info", buf);
    if (ret) {
        return ret;
    }

    if (main_config.device_name[0]) {
        ret = mqtt_publish(topic_kind::DEVICE, "name", main_config.device_name);
        if (ret) {
            return ret;
        }
    }

    return app_on_mqtt_connected();
}

static int mqtt_reset()
{
    printf("%s\n", __FUNCTION__);

    if (MQTTIsConnected(&mqtt_client)) {
        printf("is connected, closing connection\n");
        int ret = MQTTDisconnect(&mqtt_client);
        printf("MQTTDisconnect = %d\n", ret);
    }

    /*
    const int ms = 1000;
    printf("Starting MQTT reconnect timer\n");
    int ret = xTimerReset(mqtt_connect_wait_timer, pdMS_TO_TICKS(ms));
    // printf("ret=%d\n", ret);
    assert(ret == pdPASS);
    */

    return 0;
}

static void mqtt_start_reconnect(void *)
{
    // printf("Signalling mqtt reconnect\n");
    mqtt_do_connect = 1;
}

static int mqtt_init()
{
    int ret;

    mqtt_do_connect = 0;

    NetworkInit(&mqtt_network);

    unsigned int command_timeout_ms = 3000;
    MQTTClientInit(&mqtt_client, &mqtt_network, command_timeout_ms,
                   mqtt_tx_buf, sizeof(mqtt_tx_buf), mqtt_rx_buf, sizeof(mqtt_rx_buf));

    ret = format_topic(topic_command, sizeof(topic_command), topic_kind::MACHINE_ACCESS, "command");
    if (ret) {
        return ret;
    }

    ret = format_topic(topic_device_name, sizeof(topic_device_name), topic_kind::DEVICE, "name");
    if (ret) {
        return ret;
    }

    uint32_t chip_id = system_get_chip_id();
    ret = snprintf(main_config.mqtt_client_id, sizeof(main_config.mqtt_client_id), "esp-%02x%02x%02x",
                   (chip_id >> 16) & 0xff, (chip_id >> 8) & 0xff, chip_id & 0xff) >= sizeof(main_config.mqtt_client_id);
    if (ret) {
        return ret;
    }

    printf("MQTT: client id=%s\n", main_config.mqtt_client_id);

    mqtt_connect_wait_timer = xTimerCreate("mqtt_connect_wait_timer",
                                           pdMS_TO_TICKS(2000), pdFALSE, NULL, mqtt_start_reconnect);

    ret = mqtt_connect_wait_timer ? 0 : ENOMEM;

    return ret;
}

int mqtt_publish(topic_kind kind, const char *topic, const char *value)
{
    char buf[100];
    format_topic(buf, sizeof(buf), kind, topic);

    printf("%s: %s %s\n", __FUNCTION__, buf, value);

    MQTTMessage m;
    bzero(&m, sizeof(m));
    m.qos = QOS1;
    m.payloadlen = strlen(value);
    m.payload = (void *)value;
    m.retained = 0;
    int ret = MQTTPublish(&mqtt_client, buf, &m);

    // printf("%s: %s\n", __FUNCTION__, failok(ret));

    return ret;
}

static void set_station_mode()
{
    // printf("STATION_MODE\n");
    if (wifi_get_opmode_default() != NULL_MODE) {
        printf("Setting default station mode\n");
        wifi_set_opmode(NULL_MODE);
    }

    wifi_set_opmode_current(STATION_MODE);

    struct station_config sc;
    bzero(&sc, sizeof(struct station_config));
    sprintf((char *)sc.ssid, main_config.wifi_ssid);
    sprintf((char *)sc.password, main_config.wifi_password);
    wifi_station_set_config(&sc);

    wifi_set_event_handler_cb(wifi_event_handler_cb);

    printf("Connecting to AP: %s\n", main_config.wifi_ssid);
    wifi_station_set_auto_connect(true);
    wifi_station_connect();
}

static int on_got_ip()
{
    struct ip_info info;
    wifi_get_ip_info(STATION_IF, &info);
    printf("ip=" IPSTR ", nm=" IPSTR ", gw=" IPSTR "\n", IP2STR(&info.ip), IP2STR(&info.netmask), IP2STR(&info.gw));
    return mqtt_reset();
}

struct mqtt_message {
    topic_kind kind;
    char topic[20];
    char value[100];
};
struct mqtt_message mqtt_queue[2];
const int mqtt_queue_capacity = sizeof(mqtt_queue) / sizeof(mqtt_queue[0]);
int mqtt_queue_size;
SemaphoreHandle_t mqtt_queue_mutex;

int mqtt_queue_init()
{
    mqtt_queue_size = 0;
    mqtt_queue_mutex = xSemaphoreCreateMutex();
    return 0;
}

int mqtt_queue_publish(topic_kind kind, const char *topic, const char *value)
{
    int sz;
    struct mqtt_message *item;
    int ret = 1;

    xSemaphoreTake(mqtt_queue_mutex, portMAX_DELAY);

    printf("%s: mqtt_queue_size=%d, max=%d\n", __FUNCTION__, mqtt_queue_size, mqtt_queue_capacity);

    if (mqtt_queue_size == mqtt_queue_capacity) {
        goto fail;
    }

    item = &mqtt_queue[mqtt_queue_size];

    item->kind = kind;
    sz = strlcpy(item->topic, topic, sizeof(item->topic));
    if (sz >= sizeof(item->topic)) {
        printf("%s: topic too long, max=%d, topic=%s\n", __FUNCTION__, sizeof(item->topic), topic);
        goto fail;
    }

    sz = strlcpy(item->value, value, sizeof(item->value));
    if (sz >= sizeof(item->value)) {
        printf("%s: value too long, max=%d, value=%s\n", __FUNCTION__, sizeof(item->value), value);
        goto fail;
    }

    mqtt_queue_size++;
    ret = 0;

fail:
    xSemaphoreGive(mqtt_queue_mutex);
    return ret;
}

int mqtt_queue_send_all()
{
    int ret = 0;
    xSemaphoreTake(mqtt_queue_mutex, portMAX_DELAY);

    if (mqtt_queue_size) {
        // printf("Publishing %d items.\n", mqtt_queue_size);
    }

    for (int i = 0; i < mqtt_queue_size; i++) {
        struct mqtt_message *item = &mqtt_queue[i];
        mqtt_publish(topic_kind::MACHINE_ACCESS, item->topic, item->value);
    }
    mqtt_queue_size = 0;

    xSemaphoreGive(mqtt_queue_mutex);
    return ret;
}

extern "C"
void pvShowMalloc();

void main_task(void *ctx)
{
    (void) ctx;

    int count = 0;
    uint32_t notification_value;
    while (1) {
        int timeout = xTaskNotifyWait(0, UINT32_MAX, &notification_value, pdMS_TO_TICKS(1000)) == pdFALSE;

        if (notification_value & EVENTS_GOT_IP) {
            on_got_ip();
        }
        if (notification_value & EVENTS_LOST_IP) {
            mqtt_reset();
        }

        if (!MQTTIsConnected(&mqtt_client)) {
            bool has_ip = wifi_station_get_connect_status() == STATION_GOT_IP;
            if (has_ip) {
                int delay = 1000;
                printf("MQTT disconnected, reconnecting in %d ms\n", delay);
                vTaskDelay(pdMS_TO_TICKS(delay));

                int ret = mqtt_connect();
                if (ret) {
                    printf("mqtt_connect failed: %d\n", ret);
                    mqtt_reset();
                }
            }
        } else {
            mqtt_queue_send_all();
        }

        if (timeout) {
            if (count % 10 == 0) {
                printf("Hello World! count=%d, connected=%d\n", count, MQTTIsConnected(&mqtt_client));
                // pvShowMalloc();
            }
            // printf("is active: %d\n", xTimerIsTimerActive(mqtt_connect_wait_timer) != pdFALSE);
            count++;
        }
    }
}

static int app_mqtt_publish(const char *topic, const char *value)
{
    // return mqtt_queue_publish(topic_kind::MACHINE_ACCESS, topic, value);
    return mqtt_publish(topic_kind::MACHINE_ACCESS, topic, value);
}

static int app_mqtt_subscribe(const char *pattern, void (*callback)(MessageData *))
{
    printf("%s: pattern=%s\n", __FUNCTION__, pattern);
    int ret = MQTTSubscribe(&mqtt_client, pattern, QOS1, callback);
    return ret;
}

static int app_mqtt_format(char* buf, int sz, const char *pattern)
{
    return format_topic(buf, sz, topic_kind::MACHINE_ACCESS, pattern);
}

struct app_deps app_deps = {
    .mqtt_publish = app_mqtt_publish,
    .mqtt_subscribe = app_mqtt_subscribe,
    .mqtt_format = app_mqtt_format,
};

#define THREAD_NAME "main"
#define THREAD_STACK_WORDS 4096
#define THREAD_PRIO 8

void user_init()
{
    int ret;
    char buf[100];

    os_printf("SDK version: %s, free: %d, app build: %s\n", system_get_sdk_version(), system_get_free_heap_size(), __TIMESTAMP__);

    assert(fs_init() == 0);
    assert(config_load(buf, sizeof(buf)) == 0);

#ifdef WIFI_SSID
    printf("Using override wifi-ssid: %s\n", ISTR(WIFI_SSID));
    strncpy(main_config.wifi_ssid, ISTR(WIFI_SSID), sizeof(main_config.wifi_ssid));
#endif
#ifdef WIFI_PASSWORD
    printf("Using override wifi-password: %s\n", ISTR(WIFI_PASSWORD));
    strncpy(main_config.wifi_password, ISTR(WIFI_PASSWORD), sizeof(main_config.wifi_password));
#endif

    printf("Configuration:\n");
    printf("  wifi-ssid=%s\n  wifi-password=%s\n\n", main_config.wifi_ssid, main_config.wifi_password);
    printf("  mqtt-host=%s\n  mqtt-port=%d\n\n", main_config.mqtt_host, main_config.mqtt_port);
    printf("  device-name=%s\n\n", main_config.device_name[0] ? main_config.device_name : "<not set>");

    assert(mqtt_init() == 0);
    assert(mqtt_queue_init() == 0);

    assert(app_init(&app_deps) == 0);

    set_station_mode();

    ret = xTaskCreate(main_task, THREAD_NAME, THREAD_STACK_WORDS, NULL, THREAD_PRIO, &main_task_handle);
    assert(ret == pdPASS);
}
