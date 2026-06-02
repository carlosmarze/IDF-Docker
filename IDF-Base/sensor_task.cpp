// sensor_task.cpp (Nuevo archivo de implementación)
#include "utils_mqtt.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG_SENSOR = "SENSOR";

void sensor_read_and_publish(void *pvParameters) {
    char data_str[32];
    int sensor_value = 0;

    while (1) {
        // 1. Lógica de lectura del sensor
        sensor_value++; // Simulación de lectura
        
        snprintf(data_str, sizeof(data_str), "%d", sensor_value);

        // 2. Publicar el dato (Usando el manejador 'client' externo)
        if (client != NULL && esp_mqtt_client_is_connected(client)) {
            int msg_id = esp_mqtt_client_publish(
                client, 
                "/sensores/temperatura", 
                data_str, 
                0,      // Largo (0 = strlen)
                1,      // QoS (Quality of Service)
                0       // Retain (0 = no)
            );
            ESP_LOGI(TAG_SENSOR, "Mensaje publicado, ID: %d", msg_id);
        } else {
            ESP_LOGW(TAG_SENSOR, "Cliente MQTT no conectado. Saltando publicación.");
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Lee cada 10 segundos
    }
}