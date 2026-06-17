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

static char mqtt_last_payload[MQTT_MAX_PAYLOAD];
static int  mqtt_last_len = 0;

static TaskHandle_t mqtt_wd_handle = nullptr;
static SemaphoreHandle_t mqtt_mutex = nullptr;
//Para MQTT sobre SSL (ej: HiveMQ Cloud)

//char* mqtt_ca_cert = NULL;   // definición real del buffer dinámico para el certificado raíz, que se cargará desde FS

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
    //log_event_t levt; //nro de evento a loggear en la cola global del logger
    //bool log_event = false; // Si el evento es relevante para loggear (ej: desconexiones, conexiones, etc.)
    // Verificar stack disponible
    UBaseType_t stack_free = uxTaskGetStackHighWaterMark(NULL);
    if (stack_free < 1024) {
        ESP_LOGW(TAG, "Stack bajo en handler: %u bytes", stack_free);
    }

    switch (event->event_id) {

        case MQTT_EVENT_CONNECTED: {
            //LOGI("MQTT", "MQTT conectado");
            //levt = LOG_EVT_MQTT_CONNECTED;
            //log_event = true;
            //ESP_LOGI(TAG, "MQTT conectado");
            mqttconnStatus = true;

            char sub_topic[MAX_TOPIC_LENGTH];
            generar_topico_mqtt("Q", SensorID, sub_topic, sizeof(sub_topic));
            esp_mqtt_client_subscribe(client, sub_topic, 0);
            generar_topico_mqtt("A", SensorID, sub_topic, sizeof(sub_topic));
            publish_mqtt(sub_topic, "Reconexion", 0, 0); // Publicamos un mensaje de "online" al conectar para que el sistema sepa que estamos activos (puede ser útil para monitoreo o para que otros sistemas reaccionen a nuestra conexión)
            LOGI(TAG, "Conectado. Pedida suscripcion a %s", sub_topic);
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
            LOGE(TAG, "MQTT desconectado");
            //levt = LOG_EVT_MQTT_DISCONNECTED;
            //log_event = true;
            mqttconnStatus = false;
            break;
        }
        case MQTT_EVENT_DATA: {
            //write_system_log("MQTT", "MQTT data recibida");
            //levt = LOG_EVT_MQTT_DATA;
            //log_event = true;

            // Guardamos payload para que lo use el procesador de comandos
            //events_set_mqtt_payload(event->data, event->data_len);
            if (event->data_len >= MQTT_MAX_PAYLOAD)
                event->data_len = MQTT_MAX_PAYLOAD - 1;

            memcpy(mqtt_last_payload, event->data, event->data_len);
            mqtt_last_payload[event->data_len] = '\0';
            mqtt_last_len = event->data_len;
            //process_commands(
              //  CMD_SRC_MQTT,
                //events_get_mqtt_payload(),
                // ' ',
                //=',
                // -1
            //);
            ESP_LOGI(TAG, "Stack libre en mqtt pre proc: %u bytes", uxTaskGetStackHighWaterMark(NULL));
            process_commands(
                CMD_SRC_MQTT,
                mqtt_last_payload,
                ' ',
                '=',
                -1
            );
            ESP_LOGI(TAG, "Stack libre en mqtt post proc: %u bytes", uxTaskGetStackHighWaterMark(NULL));
            break;
        }
        case MQTT_EVENT_ERROR:
        {
            LOGE(TAG, "MQTT error");
            //write_system_log("MQTT", "MQTT error");
            //levt = LOG_EVT_MQTT_ERROR;
            //log_event = true;
            break;
        }
        case MQTT_EVENT_BEFORE_CONNECT:
        {
            LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;
        }
        default:
        {
            LOGW(TAG, "Evento MQTT desconocido id=%d", event->event_id);
            break;
        }

    }//fin del switch

    //if (log_event) {
        //write_system_log(TAG, log_msg.c_str());
        //xQueueSend(g_log_queue, &levt, portMAX_DELAY);
        //ESP_LOGI(TAG, "%s", log_msg.c_str()); 
    //}

    return ESP_OK;
}

static void mqtt_event_handler(void* handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void* event_data)
{
    mqtt_event_handler_cb((esp_mqtt_event_handle_t) event_data);
}


void publish_mqtt(const char* topic, const char* data, int qos, int retain) {
    if (mqttconnStatus && client) {
        esp_mqtt_client_publish(client, topic, data, 0, qos, retain);
    }
}

// --- Watchdog (solo reconnect suave, sin stop/start ni destroy) ---
void mqtt_watchdog_task(void *pvParameters) {
    //std::string log_msg = "";
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

        LOGI(TAG, "[MQTT-WD] Forzando reconexión suave (reconnect).");
        //write_system_log(TAG, log_msg.c_str());
        //ESP_LOGI(TAG, "%s", log_msg.c_str());

        if (mqtt_mutex) xSemaphoreTake(mqtt_mutex, portMAX_DELAY);
        esp_mqtt_client_reconnect(client);
        if (mqtt_mutex) xSemaphoreGive(mqtt_mutex);
    }
}

 char* load_cert_from_fs(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) {
        ESP_LOGE("MQTT", "No se pudo abrir certificado: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(size + 1);
    if (!buf) {
        ESP_LOGE("MQTT", "No hay memoria para certificado (%ld bytes)", size);
        fclose(f);
        return NULL;
    }

    fread(buf, 1, size, f);
    buf[size] = '\0';  // PEM debe terminar en null
    fclose(f);

    return buf;
}


char* mqtt_ca_cert = NULL;
void mqtt_app_start_secure(void) {
    
    LOGI("MQTT", "Iniciando MQTT sobre SSL con HiveMQ Cloud Host %s User %s...", MQTT_HIVEMQ_HOST, MQTT_HIVEMQ_USERNAME);
    if (mqtt_initialized) return;

    // 1. Cargar certificado desde LittleFS
    mqtt_ca_cert = load_cert_from_fs(MQTT_HIVEMQ_ROOT_CERT_PEM); // /fs/certs/hivemq_ca.pem
    if (!mqtt_ca_cert) {
        LOGE("MQTT", "No se pudo cargar CA desde FS");
        return;
    }

    LOGI("MQTT", "Certificado CA cargado correctamente");

    // 2. Configurar MQTT con TLS
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .hostname = MQTT_HIVEMQ_HOST,
                .transport = MQTT_TRANSPORT_OVER_SSL,
                .port = 8883,
            },
            .verification = {
                .certificate = mqtt_ca_cert,   // <--- ACÁ USAMOS EL CERTIFICADO DEL FS
            },
        },
        .credentials = {
            .username = MQTT_HIVEMQ_USERNAME,
            .authentication = {
                .password = MQTT_HIVEMQ_PASSWORD,
            },
        },
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);

    if (mqtt_mutex) xSemaphoreTake(mqtt_mutex, portMAX_DELAY);
    esp_mqtt_client_start(client);
    if (mqtt_mutex) xSemaphoreGive(mqtt_mutex);

    if (!mqtt_wd_handle) {
        xTaskCreate(mqtt_watchdog_task, "mqtt_watchdog", 4096, NULL, 3, &mqtt_wd_handle);
    }
    LOGI("MQTT", "MQTT sobre SSL iniciado con HiveMQ Cloud");
    mqtt_initialized = true;
}


void mqtt_app_start_insecure(void) {
    LOGI("MQTT", "Iniciando MQTT con MyqttHub...");
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
    LOGI("MQTT", "MQTT iniciado con MyqttHub");
}

void mqtt_app_start(void) {
    if(MqttTLS) {
        LOGI("MQTT", "Iniciando MQTT TLS...");
        mqtt_app_start_secure();
    } else {
        LOGI("MQTT", "Iniciando MQTT sin TLS...");
        mqtt_app_start_insecure();
    }
    
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
