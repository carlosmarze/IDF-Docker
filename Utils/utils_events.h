#pragma once

//#define MQTT_MAX_PAYLOAD 1024

void events_set_last_ip(const char* ip);
const char* events_get_last_ip();

void events_set_mqtt_payload(const char* data, int len);
const char* events_get_mqtt_payload();
int events_get_mqtt_payload_len();
