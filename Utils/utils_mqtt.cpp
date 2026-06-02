#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "MisVariablesProyecto.h"
#include "utils_wifi.h"
#include "utils_mqtt.h"
#include "utils_webs.h"
#include "utils_cmd_processor.h"
#include "utils_config.h" // Para g_ota_en_progreso
#include "utils_logger.h" // Para logging
#include "utils_events.h"

//buffer MQTT
//static char mqtt_last_payload[MQTT_MAX_PAYLOAD];
//static int mqtt_last_len = 0;

bool mqttconnStatus = false;
static const char *TAG = "MQTT_TASK";
esp_mqtt_client_handle_t client = nullptr;
static bool mqtt_initialized = false;

static TaskHandle_t mqtt_wd_handle = nullptr;
static SemaphoreHandle_t mqtt_mutex = nullptr;

//chequeo externo de status MQTT para otras tareas (ej: watchdog)
bool mqtt_is_initialized(void) {
    return mqtt_initialized;
}

// Función para generar tópicos
void generar_topico_mqtt(const char* tipo, int id, char* topic_out, size_t max_len) {
    snprintf(topic_out, max_len, "%s%s/%d", MQTT_TOPIC_BASE, tipo, id);
}


static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    log_event_t levt; //nro de evento a loggear en la cola global del logger
    bool log_event = false; // Si el evento es relevante para loggear (ej: desconexiones, conexiones, etc.)

    switch (event->event_id) {

        case MQTT_EVENT_CONNECTED: {
            //write_system_log("MQTT", "MQTT conectado");
            levt = LOG_EVT_MQTT_CONNECTED;
            log_event = true;
            //ESP_LOGI(TAG, "MQTT conectado");
            mqttconnStatus = true;

            char sub_topic[MAX_TOPIC_LENGTH];
            generar_topico_mqtt("Q", SensorID, sub_topic, sizeof(sub_topic));
            esp_mqtt_client_subscribe(client, sub_topic, 0);

            ESP_LOGI("MQTT", "Suscrito a %s", sub_topic);
            break;
        }
        case MQTT_EVENT_SUBSCRIBED: {

            ESP_LOGI(TAG, "Suscripción MQTT confirmada, msg_id=%d", event->msg_id);
            break;
        }
        case MQTT_EVENT_UNSUBSCRIBED: {

            ESP_LOGI(TAG, "Suscripción MQTT terminada, msg_id=%d", event->msg_id);
            break;
        }

        case MQTT_EVENT_DISCONNECTED: {
            //write_system_log("MQTT", "MQTT desconectado");
            levt = LOG_EVT_MQTT_DISCONNECTED;
            log_event = true;
            mqttconnStatus = false;
            break;
        }
        case MQTT_EVENT_DATA: {
            //write_system_log("MQTT", "MQTT data recibida");
            levt = LOG_EVT_MQTT_DATA;
            log_event = true;

            // Guardamos payload para que lo use el procesador de comandos
            events_set_mqtt_payload(event->data, event->data_len);

            process_commands(
                CMD_SRC_MQTT,
                events_get_mqtt_payload(),
                ' ',
                '=',
                -1
            );
            break;
        }
        case MQTT_EVENT_ERROR:
        {
            //write_system_log("MQTT", "MQTT error");
            levt = LOG_EVT_MQTT_ERROR;
            log_event = true;
            break;
        }
        default:
        {
            ESP_LOGW("MQTT", "Evento MQTT desconocido id=%d", event->event_id);
            break;
        }

    }//fin del switch

    if (log_event) {
        //write_system_log(TAG, log_msg.c_str());
        xQueueSend(g_log_queue, &levt, portMAX_DELAY);
        //ESP_LOGI(TAG, "%s", log_msg.c_str()); 
    }

    return ESP_OK;
}

static void mqtt_event_handler(void* handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void* event_data)
{
    mqtt_event_handler_cb((esp_mqtt_event_handle_t) event_data);
}

static void mqtt_handler_OLD(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    std::string log_msg = "";
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        log_msg = "MQTT_EVENT_CONNECTED";
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG, "%s", log_msg.c_str());

        //ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        
        mqttconnStatus = true;

        char sub_topic[MAX_TOPIC_LENGTH];
        generar_topico_mqtt("Q", SensorID, sub_topic, sizeof(sub_topic));
        esp_mqtt_client_subscribe(client, sub_topic, 0);
        ESP_LOGI(TAG, "Suscrito a tópico de comandos: %s", sub_topic);
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        log_msg = "MQTT_EVENT_DISCONNECTED";
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG, "%s", log_msg.c_str());
        //ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqttconnStatus = false;
        break;

    case MQTT_EVENT_DATA: {
        log_msg = "MQTT_EVENT_DATA recibido";
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG, "%s", log_msg.c_str());

        //ESP_LOGI(TAG, "MQTT_EVENT_DATA recibido");
        char *data = (char *)malloc(event->data_len + 1);
        if (data) {
            memcpy(data, event->data, event->data_len);
            data[event->data_len] = '\0';

            process_commands(CMD_SRC_MQTT, data, ' ', '=', -1);

            free(data);
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        log_msg = "MQTT_EVENT_ERROR";
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG, "%s", log_msg.c_str());
        //ESP_LOGI(TAG, "MQTT_EVENT_ERROR");    
        
        break;

    default:
        break;
    }
}

void publish_mqtt(const char* topic, const char* data, int qos, int retain) {
    if (mqttconnStatus && client) {
        esp_mqtt_client_publish(client, topic, data, 0, qos, retain);
    }
}

// --- Watchdog (solo reconnect suave, sin stop/start ni destroy) ---
void mqtt_watchdog_task(void *pvParameters) {
    std::string log_msg = "";
    int mqtt_fail_count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));

        // 1. Si hay OTA, no tocamos nada
        if (g_ota_en_progreso) {
            mqtt_fail_count = 0;
            continue;
        }

        // 2. Si WiFi NO está realmente operativo, no tocar MQTT
        if (!wifi_ready) {
            mqtt_fail_count = 0;
            continue;
        }

        // 3. Si MQTT está conectado, todo bien
        if (mqttconnStatus && (client != nullptr)) {
            mqtt_fail_count = 0;
            continue;
        }

        // 4. Si MQTT no está inicializado o el cliente es NULL, no hacer nada
        if (!mqtt_initialized || client == nullptr) {
            mqtt_fail_count = 0;
            continue;
        }

        // 5. Llegamos aquí: WiFi OK, MQTT desconectado → intentar reconnect suave
        mqtt_fail_count++;
        printf("[MQTT-WD] Desconectado. Intento %d...\n", mqtt_fail_count);

        log_msg = "[MQTT-WD] Forzando reconexión suave (reconnect).";
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG, "%s", log_msg.c_str());

        if (mqtt_mutex) xSemaphoreTake(mqtt_mutex, portMAX_DELAY);
        esp_mqtt_client_reconnect(client);
        if (mqtt_mutex) xSemaphoreGive(mqtt_mutex);
    }
}



void mqtt_app_start(void) {
    if (mqtt_initialized) return;

    if (!mqtt_mutex) {
        mqtt_mutex = xSemaphoreCreateMutex();
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .hostname = MQTT_BROKER_HOST,
                .transport = MQTT_TRANSPORT_OVER_TCP,
                .port = MQTT_BROKER_PORT,
            },
        },
        .credentials = {
            .username = MQTT_BROKER_USERNAME,
            .authentication = {
                .password = MQTT_BROKER_PASSWORD,
            },
        },
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    if (mqtt_mutex) xSemaphoreTake(mqtt_mutex, portMAX_DELAY);
    esp_mqtt_client_start(client);
    if (mqtt_mutex) xSemaphoreGive(mqtt_mutex);

    if (!mqtt_wd_handle) {
        xTaskCreate(mqtt_watchdog_task, "mqtt_watchdog", 4096, NULL, 3, &mqtt_wd_handle);
    }

    mqtt_initialized = true;
}

void mqtt_app_stop(void) {
    if (!mqtt_initialized) return;

    if (mqtt_wd_handle) {
        vTaskDelete(mqtt_wd_handle);
        mqtt_wd_handle = nullptr;
    }

    if (client) {
        if (mqtt_mutex) xSemaphoreTake(mqtt_mutex, portMAX_DELAY);
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        if (mqtt_mutex) xSemaphoreGive(mqtt_mutex);
        client = nullptr;
    }

    mqtt_initialized = false;
    mqttconnStatus = false;
}
