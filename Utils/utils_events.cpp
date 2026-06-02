#include "utils_events.h"
#include "utils_logger.h"
#include "utils_config.h"
#include "utils_mqtt.h"
#include "utils_wifi.h"
#include "utils_webs.h"
#include "utils_http.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static char last_ip_str[16] = "0.0.0.0";
static char mqtt_last_payload[MQTT_MAX_PAYLOAD];
static int  mqtt_last_len = 0;

void events_set_last_ip(const char* ip)
{
    strncpy(last_ip_str, ip, sizeof(last_ip_str));
    last_ip_str[sizeof(last_ip_str) - 1] = '\0';
}

const char* events_get_last_ip()
{
    return last_ip_str;
}

void events_set_mqtt_payload(const char* data, int len)
{
    if (len >= MQTT_MAX_PAYLOAD)
        len = MQTT_MAX_PAYLOAD - 1;

    memcpy(mqtt_last_payload, data, len);
    mqtt_last_payload[len] = '\0';
    mqtt_last_len = len;
}

const char* events_get_mqtt_payload()
{
    return mqtt_last_payload;
}

int events_get_mqtt_payload_len()
{
    return mqtt_last_len;
}
