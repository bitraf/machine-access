#include "esp_misc.h"
#include "esp_sta.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "spiffs.h"
#include "spiffs.h"
#include "MQTTClient.h"

#include "sdkconfig.h"
#include "main.h"

#include <assert.h>
#include <errno.h>

// Allow overriding configuration from Kconfig.
#define ISTR(x) STR(x)
#define STR(x) #x

#ifdef WIFI_SSID
#define CFG_WIFI_SSID ISTR(WIFI_SSID)
#else
#define CFG_WIFI_SSID CONFIG_MAIN_WIFI_SSID_DEFAULT
#endif

#ifdef WIFI_PASSWORD
#define CFG_WIFI_PASSWORD ISTR(WIFI_PASSWORD)
#else
#define CFG_WIFI_PASSWORD CONFIG_MAIN_WIFI_PASSWORD_DEFAULT
#endif

#ifdef MQTT_HOST
#define CFG_MQTT_HOST ISTR(MQTT_HOST)
#else
#define CFG_MQTT_HOST CONFIG_MAIN_MQTT_HOST_DEFAULT
#endif

#ifdef MQTT_PORT
#define CFG_MQTT_PORT MQTT_PORT
#else
#define CFG_MQTT_PORT CONFIG_MAIN_MQTT_PORT_DEFAULT
#endif

struct main_config {
    char wifi_ssid[20] = CFG_WIFI_SSID;
    char wifi_password[20] = CFG_WIFI_PASSWORD;

    char mqtt_host[20] = CFG_MQTT_HOST;
    int mqtt_port = CFG_MQTT_PORT;
    char mqtt_client_id[20] = {0};
} main_config;

extern "C"
uint32_t user_rf_cal_sector_set();

extern "C"
void user_init();

enum events {
    EVENTS_GOT_IP = 1 << 0,
    EVENTS_LOST_IP = 1 << 1,
    EVENTS_CONNECT_MQTT = 1 << 2,
};

xTaskHandle main_task_handle = 0;

static const char *failok(int ret) {
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

static int format_topic(char *buf, size_t sz, const char *suffix)
{
    uint32_t chip_id = system_get_chip_id();
    int count = snprintf(buf, sz, CONFIG_MAIN_MQTT_PREFIX "/%02x%02x%02x/%s",
            (chip_id >> 16) & 0xff, (chip_id >> 8) & 0xff, chip_id & 0xff, suffix);
    return count < sz ? 0 : ENOMEM;
}

static MQTTClient mqtt_client;
static Network mqtt_network;
unsigned char mqtt_tx_buf[1000], mqtt_rx_buf[1000];

static int mqtt_connect() {
    int ret;
    char buf[100];

    ret = format_topic(buf, sizeof(buf), "online");
    if (ret) {
        return ret;
    }

    MQTTPacket_connectData cd = MQTTPacket_connectData_initializer;
    cd.keepAliveInterval = 15;
    cd.willFlag = 1;
    cd.will.retained = 1;
    cd.will.topicName = MQTTString_initializer;
    cd.will.topicName.cstring = buf;
    cd.will.message = MQTTString_initializer;
    cd.will.message.cstring = (char *)"0";

    NetworkInit(&mqtt_network);

    unsigned int command_timeout_ms = 3000;
    MQTTClientInit(&mqtt_client, &mqtt_network, command_timeout_ms,
            mqtt_tx_buf, sizeof(mqtt_tx_buf), mqtt_rx_buf, sizeof(mqtt_rx_buf));

    if ((ret = NetworkConnect(&mqtt_network, main_config.mqtt_host, main_config.mqtt_port)) != 0) {
        printf("%s: NetworkConnect:%d\n", __FUNCTION__, ret);
        goto fail;
    }

    // printf("%s: network connected\n", __FUNCTION__);

    cd.MQTTVersion = 4;
    cd.clientID.cstring = main_config.mqtt_client_id;

    if ((ret = MQTTConnect(&mqtt_client, &cd)) != 0) {
        printf("%s: MQTTConnect:%d\n", __FUNCTION__, ret);
        goto fail;
    }
    // printf("%s: mqtt connected\n", __FUNCTION__);

#if defined(MQTT_TASK)
    if ((ret = MQTTStartTask(&mqtt_client)) != pdPASS) {
        printf("%s: MQTTStartTask:%d\n", __FUNCTION__, ret);
        goto fail;
    }
#endif

    return 0;

fail:
    return ret;
}

static int mqtt_disconnect() {
    printf("%s\n", __FUNCTION__);

    MQTTDisconnect(&mqtt_client);
    // TODO: start timer to reconnect to mqtt

    return 0;
}

// Topics that we subscribe to can't be stack allocated
char topic_lock[100];
char topic_unlock[100];

static int mqtt_init() {
    int ret;

    ret = format_topic(topic_lock, sizeof(topic_lock), "lock");
    if (ret) {
        return ret;
    }

    ret = format_topic(topic_unlock, sizeof(topic_unlock), "unlock");
    if (ret) {
        return ret;
    }

    uint32_t chip_id = system_get_chip_id();
    ret = snprintf(main_config.mqtt_client_id, sizeof(main_config.mqtt_client_id), "esp-%02x%02x%02x",
            (chip_id >> 16) & 0xff, (chip_id >> 8) & 0xff, chip_id & 0xff) >= sizeof(main_config.mqtt_client_id);

    return ret;
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

static void on_message(MessageData *data) {
    static const char lock[] = "/lock";
    static const char unlock[] = "/unlock";
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

    if (memcmp(&topic[topic_len - sizeof(lock) + 1], lock, sizeof(lock)) == 0) {
        printf("LOCK \n");
        app_on_lock(data->message);
    } else if (memcmp(&topic[topic_len - sizeof(unlock) + 1], unlock, sizeof(unlock)) == 0) {
        printf("UNLOCK \n");
        app_on_unlock(data->message);
    } else {
        char buf2[100];
        buf_to_cstr(buf2, sizeof(buf2), data->message->payload, data->message->payloadlen);
        ret = ENODEV;
        goto fail;
    }

    return;

fail:
    printf("FAIL: err=%d\n", ret);
}

static int mqtt_publish(const char *topic, const char *value) {
    char buf[100];
    format_topic(buf, sizeof(buf), topic);

    printf("%s: %s %s\n", __FUNCTION__, buf, value);

    MQTTMessage m;
    bzero(&m, sizeof(m));
    m.qos = QOS1;
    m.payloadlen = strlen(value);
    m.payload = (void *)value;
    m.retained = 1;
    int ret = MQTTPublish(&mqtt_client, buf, &m);

    printf("%s: %s\n", __FUNCTION__, failok(ret));

    return ret;
}

static void set_station_mode() {
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
    if (mqtt_connect()) {
        printf("mqtt_connect failed\n");
        mqtt_disconnect();
        return ECONNRESET;
    }

    int ret = MQTTSubscribe(&mqtt_client, topic_lock, QOS2, on_message);
    if (ret) {
        return ret;
    }

    ret = MQTTSubscribe(&mqtt_client, topic_unlock, QOS2, on_message);
    if (ret) {
        return ret;
    }

    ret = mqtt_publish("online", "1");
    if (ret) {
        return ret;
    }

    char buf[100];
    int sz = snprintf(buf, sizeof(buf), "build-rev: %s\nbuild-timestamp: %s\n", MAIN_GIT_REV, __TIMESTAMP__);

    if (sz >= sizeof(buf)) {
        return ENOMEM;
    }

    ret = mqtt_publish("info", buf);
    if (ret) {
        return ret;
    }

    return app_on_mqtt_connected();
}

void main_task(void* ctx) {
    (void) ctx;

    int count = 0;
    uint32_t notification_value;
    int last_connected = 0;
    while (1) {
        int timeout = xTaskNotifyWait(0, UINT32_MAX, &notification_value, pdMS_TO_TICKS(1000)) == pdFALSE;

        if (notification_value & EVENTS_GOT_IP) {
            on_got_ip();
        }
        if (notification_value & EVENTS_LOST_IP) {
            mqtt_disconnect();
        }

        int is_connected = MQTTIsConnected(&mqtt_client);
        if (is_connected != last_connected) {
            last_connected = is_connected;

            if (is_connected) {
                printf("MQTT connected\n");
            } else {
                printf("MQTT disconnected\n");
                mqtt_disconnect();
            }
        }

        if (timeout) {
            if (count % 10 == 0)
            printf("Hello World! connected=%d\n", mqtt_client.isconnected);
            count++;
        }
    }
}

struct app_deps app_deps = {
    .mqtt_publish = mqtt_publish
};

#define THREAD_NAME "main"
#define THREAD_STACK_WORDS 2048
#define THREAD_PRIO 8

void user_init() {
    int ret;

    os_printf("SDK version: %s, free: %d, app build: %s\n", system_get_sdk_version(), system_get_free_heap_size(), __TIMESTAMP__);

    assert(mqtt_init() == 0);

    printf("Configuration:\n");
    printf("  wifi-ssid=%s\n  wifi-password=%s\n\n", main_config.wifi_ssid, main_config.wifi_password);
    printf("  mqtt-host=%s\n  mqtt-port=%d\n  mqtt-client-id=%s\n\n", main_config.mqtt_host, main_config.mqtt_port, main_config.mqtt_client_id);

    assert(app_init(&app_deps) == 0);

    set_station_mode();

    ret = xTaskCreate(main_task, THREAD_NAME, THREAD_STACK_WORDS, NULL, THREAD_PRIO, &main_task_handle);
    assert(ret == pdPASS);
}
