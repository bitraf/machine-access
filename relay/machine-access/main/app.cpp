#include "main.h"

#include <stdio.h>

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

const struct app_deps * deps;

int app_init(struct app_deps * const deps)
{
    printf("%s: \n", __FUNCTION__);

    ::deps = deps;

    lock_state = lock_state_t::LOCKED;

    return 0;
}

int app_on_mqtt_connected()
{
    return deps->mqtt_publish("is_locked", lock_state == lock_state_t::LOCKED ? "true" : "false");
}

void app_on_lock(MQTTMessage* msg)
{
    printf("%s: \n", __FUNCTION__);

    if (lock_state == lock_state_t::LOCKED) {
        printf("Lock already locked.");
    } else {
        lock_state = lock_state_t::LOCKED;
    }

    deps->mqtt_publish("is_locked", "true");
}

void app_on_unlock(MQTTMessage* msg)
{
    printf("%s: \n", __FUNCTION__);

    if (lock_state == lock_state_t::UNLOCKED) {
        printf("Lock already unlocked.");
    } else {
        lock_state = lock_state_t::UNLOCKED;
    }

    deps->mqtt_publish("is_locked", "false");
}
