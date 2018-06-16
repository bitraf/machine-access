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

enum class topic_kind {
    DEVICE,
    MACHINE_ACCESS,
};

int mqtt_publish(topic_kind kind, const char *topic, const char *value);
int buf_to_cstr(char *dst, const int dst_sz, const void *src, const int src_sz);
int mqtt_string_to_cstr(char *buf, int sz, MQTTString *str);

struct main_config {
    char wifi_ssid[20] = CFG_WIFI_SSID;
    char wifi_password[20] = CFG_WIFI_PASSWORD;

    char mqtt_host[20] = CFG_MQTT_HOST;
    int mqtt_port = CFG_MQTT_PORT;
    char mqtt_client_id[20] = {0};
} main_config;

enum events {
    EVENTS_GOT_IP = 1 << 0,
    EVENTS_LOST_IP = 1 << 1,
    EVENTS_CONNECT_MQTT = 1 << 2,
};

xTaskHandle main_task_handle = 0;

struct _reent fs_reent;

/*
static int config_fd;

static int config_read(void *ctx, void *buf, int sz)
{
    int fd = *((int *) ctx);

    return _spiffs_read_r(&fs_reent, fd, buf, sz);
}

static int config_set_pos(void *ctx, int offset)
{
    int fd = *((int *) ctx);

    return _spiffs_lseek_r(&fs_reent, fd, offset, SEEK_SET);
}
*/

static const char *CONFIG_ITEM_WIFI_SSID = "wifi-ssid";
static const char *CONFIG_ITEM_WIFI_PASSWORD = "wifi-password";

static int config_on_item(void *ctx, const char *key, const char *value)
{
    printf("got item: '%s'='%s'\n", key, value);
    if (strcmp(CONFIG_ITEM_WIFI_SSID, key) == 0) {
        strncpy(main_config.wifi_ssid, value, sizeof(main_config.wifi_ssid));
    } else if (strcmp(CONFIG_ITEM_WIFI_PASSWORD, key) == 0) {
        strncpy(main_config.wifi_password, value, sizeof(main_config.wifi_password));
    }
    return 0;
}

static void config_make_default()
{
    strncpy(main_config.wifi_ssid, CFG_WIFI_SSID, sizeof(main_config.wifi_ssid));
    strncpy(main_config.wifi_password, CFG_WIFI_PASSWORD, sizeof(main_config.wifi_password));

    strncpy(main_config.mqtt_host, CFG_MQTT_HOST, sizeof(main_config.mqtt_host));
    main_config.mqtt_port = CFG_MQTT_PORT;
}

/**
 * Returns 0 on success. Check fs_reent._errno on failure.
 */
static int config_store()
{
    char buf[100];
    int sz;

    int fd = _spiffs_open_r(&fs_reent, "config.tmp", O_WRONLY | O_CREAT, 0);

    if (fd == -1) {
        return 1;
    }

    sz = kv_write_str(buf, sizeof(buf), CONFIG_ITEM_WIFI_SSID, main_config.wifi_ssid);
    if (_spiffs_write_r(&fs_reent, fd, buf, sz) != sz) {
        return 1;
    }

    sz = kv_write_str(buf, sizeof(buf), CONFIG_ITEM_WIFI_PASSWORD, main_config.wifi_password);
    if (_spiffs_write_r(&fs_reent, fd, buf, sz) != sz) {
        return 1;
    }

    if (_spiffs_close_r(&fs_reent, fd) == -1) {
        return 1;
    }

    if (_spiffs_rename_r(&fs_reent, "config.tmp", "config") == -1) {
        return 1;
    }

    return 0;
}

static int config_load(char *buf, int sz)
{
    int ret;
    char kbuf[20];
    char vbuf[100];
    char c;
    int read;
    struct kv_parser parser;

    // _spiffs_unlink_r(&fs_reent, "config");
    int fd = _spiffs_open_r(&fs_reent, "config", O_RDONLY, 0);
    if (fd < 0) {
        if (fs_reent._errno != ENOENT) {
            return ENOENT;
        }
        printf("No config file found, creating default configuration\n");
        config_make_default();
        if (config_store()) {
            printf("Failed to write new config, errno=%d\n", fs_reent._errno);
            return 1;
        }
        return 0;
    }

    ret = kv_parser_init(&parser, config_on_item, NULL, kbuf, sizeof(kbuf), vbuf, sizeof(vbuf));

    while ((read = _spiffs_read_r(&fs_reent, fd, &c, 1)) > 0) {
        ret = kv_parser_add(&parser, &c, 1);

        if (ret) {
            return ret;
        }
    }
    ret = kv_parser_end(&parser);

    return ret;
}

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

static void on_message(MessageData *data)
{
    static const char command[] = "/command";
    static char topic[100];
    static int topic_len;

    int ret;

    printf("%s: ", __FUNCTION__);

    topic_len = mqtt_string_to_cstr(topic, sizeof(topic), data->topicName);
    if (topic_len < 0) {
        ret = -topic_len;
        goto fail;
    }
    printf("topic=%s ", topic);

    if (1) {
        char tmp[100];
        buf_to_cstr(tmp, sizeof(tmp), data->message->payload, data->message->payloadlen);
        printf("msg.len=%d, payload: ", data->message->payloadlen);
        puts(tmp);
    }

    if (topic_len < 10) {
        // Silly short message. Won't happen as long as the
        // subscriptions and MQTT broker are sane.
        ret = ENOMEM;
        goto fail;
    }

    if (memcmp(&topic[topic_len - sizeof(command) + 1], command, sizeof(command)) == 0) {
        printf("COMMAND \n");
        app_on_command(data->message);
    } else {
        char buf2[100];
        int sz = snprintf(buf2, sizeof(buf2), "Bad topic: %s", topic);
        if (sz < sizeof(buf2)) {
            mqtt_publish(topic_kind::MACHINE_ACCESS, "error", buf2);
        }

        buf_to_cstr(buf2, sizeof(buf2), data->message->payload, data->message->payloadlen);
        ret = ENODEV;
        goto fail;
    }

    return;

fail:
    printf("FAIL: err=%d\n", ret);
}

// ------------------------------------------------------------
// MQTT
//

static int format_topic(char *buf, size_t sz, topic_kind kind, const char *suffix)
{
    uint32_t chip_id = system_get_chip_id();
    int count = snprintf(buf, sz, CONFIG_MAIN_MQTT_PREFIX "/%s/%02x%02x%02x/%s",
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

    ret = MQTTSubscribe(&mqtt_client, topic_command, QOS2, on_message);
    if (ret) {
        return ret;
    }

    ret = mqtt_publish(topic_kind::DEVICE, "online", "1");
    if (ret) {
        return ret;
    }

    int sz = snprintf(buf, sizeof(buf), "build-rev: %s\nbuild-timestamp: %s\n", MAIN_GIT_REV, __TIMESTAMP__);

    if (sz >= sizeof(buf)) {
        return ENOMEM;
    }

    ret = mqtt_publish(topic_kind::DEVICE, "info", buf);
    if (ret) {
        return ret;
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

    printf("%s: %s\n", __FUNCTION__, failok(ret));

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

    wifi_station_connect();
}

static int on_got_ip()
{
    struct ip_info info;
    wifi_get_ip_info(STATION_IF, &info);
    printf("ip=" IPSTR ", nm=" IPSTR ", gw=" IPSTR "\n", IP2STR(&info.ip), IP2STR(&info.netmask), IP2STR(&info.gw));
    return mqtt_reset();
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
    return mqtt_publish(topic_kind::MACHINE_ACCESS, topic, value);
}

struct app_deps app_deps = {
    .mqtt_publish = app_mqtt_publish
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
#warning Using override wifi-ssid
    printf("Using override wifi-ssid: %s\n", ISTR(WIFI_SSID));
    strncpy(main_config.wifi_ssid, ISTR(WIFI_SSID), sizeof(main_config.wifi_ssid));
#endif
#ifdef WIFI_PASSWORD
#warning Using override wifi-password
    printf("Using override wifi-password: %s\n", ISTR(WIFI_PASSWORD));
    strncpy(main_config.wifi_password, ISTR(WIFI_PASSWORD), sizeof(main_config.wifi_password));
#endif

    printf("Configuration:\n");
    printf("  wifi-ssid=%s\n  wifi-password=%s\n\n", main_config.wifi_ssid, main_config.wifi_password);
    printf("  mqtt-host=%s\n  mqtt-port=%d\n\n", main_config.mqtt_host, main_config.mqtt_port);

    assert(mqtt_init() == 0);

    assert(app_init(&app_deps) == 0);

    set_station_mode();

    ret = xTaskCreate(main_task, THREAD_NAME, THREAD_STACK_WORDS, NULL, THREAD_PRIO, &main_task_handle);
    assert(ret == pdPASS);
}
