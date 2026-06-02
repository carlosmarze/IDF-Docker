#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_crt_bundle.h" //para certificados raíz
//#include <sys/socket.h>
//#include <net/if.h>
#include "utils_ota_worker.h"
#include "utils_wifi.h" // Para esperar a que Wi-Fi esté conectado
#include "utils_logger.h"
#include "utils_config.h" // Para g_ota_en_progreso
#include "utils_mqtt.h" // Para mqtt_app_stop() y mqtt_app_start()

static const char *TAG = "OTA_WORKER";
static QueueHandle_t s_ota_q = NULL;

// -----------------------------------------------------------------------------
// Funciones auxiliares
// -----------------------------------------------------------------------------

// Handler HTTP (stub, se puede extender si querés loguear eventos)
static esp_err_t http_event_handler(esp_http_client_event_handle_t evt) {
    return ESP_OK;
}

// Verifica SHA256 esperado contra el de la imagen
static bool ota_verify_sha256(const char *expected_hex,
                              const uint8_t *actual,
                              size_t len) {
    if (!expected_hex || !*expected_hex || len != 32) return true; //Si no tiene código de verificación de SHA256, no chequea

    for (int i = 0; i < 32; ++i) {
        char hi = expected_hex[i * 2];
        char lo = expected_hex[i * 2 + 1];
        int v = 0;

        if      (hi >= '0' && hi <= '9') v = (hi - '0') << 4;
        else if (hi >= 'a' && hi <= 'f') v = (hi - 'a' + 10) << 4;
        else if (hi >= 'A' && hi <= 'F') v = (hi - 'A' + 10) << 4;
        else return false;

        if      (lo >= '0' && lo <= '9') v |= (lo - '0');
        else if (lo >= 'a' && lo <= 'f') v |= (lo - 'a' + 10);
        else if (lo >= 'A' && lo <= 'F') v |= (lo - 'A' + 10);
        else return false;

        if (actual[i] != (uint8_t)v) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Tarea principal OTA
// -----------------------------------------------------------------------------



static void ota_worker_task(void *arg) {
    
    std::string log_msg = "";
    ota_job_t job = {};
    // Esperar a que el grupo de eventos esté listo
    while (s_wifi_event_group == NULL) vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // 1. ESPERAR TRABAJO (Bloqueado aquí hasta que llegue un comando OTA)
        if (xQueueReceive(s_ota_q, &job, portMAX_DELAY) != pdTRUE) continue;

        g_ota_en_progreso = true; 
        // Eliminamos mqtt_app_stop(); <-- Dejamos que MQTT viva hasta que estemos SEGUROS de que hay un binario.

        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        
        ESP_LOGI(TAG, "Iniciando chequeo OTA: %s", job.url);

        esp_http_client_config_t config = {};
        config.url = job.url;
        config.timeout_ms = 15000;
        config.crt_bundle_attach = esp_crt_bundle_attach; 

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (esp_http_client_open(client, 0) != ESP_OK) {
            log_msg = "Error: No se pudo abrir conexión con el servidor OTA";
            write_system_log(TAG, log_msg.c_str());
            ESP_LOGE(TAG, "%s", log_msg.c_str());
            //ESP_LOGE(TAG, "Error: No se pudo conectar al servidor");
            esp_http_client_cleanup(client);
            g_ota_en_progreso = false;
            continue; 
        }

        esp_http_client_fetch_headers(client);
        
        char *upgrade_data_buf = (char *)malloc(2048);
        esp_ota_handle_t update_handle = 0;
        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

        int binary_size = 0;
        bool ota_started = false;
        bool header_checked = false;
        bool otasuccess = false;

        while (1) {
            int data_read = esp_http_client_read(client, upgrade_data_buf, 2048);
            if (data_read < 0) break; 
            if (data_read == 0) {
                if (ota_started) otasuccess = true;
                break;
            }

            // --- CRUCIAL: Verificación de Cabecera antes de borrar Flash ---
            if (!header_checked) {
                header_checked = true;
                if ((uint8_t)upgrade_data_buf[0] != 0xE9) {
                    // No es un binario. Probablemente es el texto "No hay archivo nuevo"
                    upgrade_data_buf[data_read < 63 ? data_read : 63] = '\0'; // Asegurar null terminator
                    
                    log_msg = "Servidor respondió: " + std::string(upgrade_data_buf);
                    write_system_log(TAG, log_msg.c_str());
                    ESP_LOGW(TAG,  "%s", log_msg.c_str());
                   
                    break; // Salimos del bucle sin haber llamado a esp_ota_begin
                }
                
                // Si llegamos aquí, ES un binario. Ahora sí, preparamos el sistema:
                log_msg = "Binario detectado. Iniciando escritura ";
                write_system_log(TAG, log_msg.c_str());
                ESP_LOGI(TAG,  "%s", log_msg.c_str());

                //ESP_LOGI(TAG, "Binario detectado. Iniciando escritura...");
                mqtt_app_stop(); // <-- Ahora sí lo detenemos si quieres máxima seguridad
                esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                ota_started = true;
            }

            if (ota_started) {
                esp_ota_write(update_handle, (const void *)upgrade_data_buf, data_read);
                binary_size += data_read;
            }
        }

        // Limpieza
        free(upgrade_data_buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (otasuccess) {
            esp_err_t err = esp_ota_end(update_handle);
            if (err == ESP_OK) {
                esp_ota_set_boot_partition(update_partition);
                log_msg = "OTA Ok. Reiniciando..."; 
                write_system_log(TAG, log_msg.c_str());
                ESP_LOGI(TAG,  "%s", log_msg.c_str());
                //ESP_LOGI(TAG, "OTA Ok. Reiniciando...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
        } else {
            if (ota_started) esp_ota_abort(update_handle);
            // Si no llegamos a empezar el OTA, simplemente volvemos a la normalidad
            g_ota_en_progreso = false;
            // IMPORTANTE: Si detuvimos el MQTT, hay que volver a arrancarlo
            mqtt_app_start(); 
        }
        
        log_msg = "Ciclo OTA terminado. Esperando..."; 
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG,  "%s", log_msg.c_str());
        //ESP_LOGI(TAG, "Ciclo OTA terminado. Esperando...");
    }
}
/* Función anterior
static void ota_worker_task(void *arg) {
    ota_job_t job = {};
    
    // Esperar a que el grupo de eventos esté listo
    while (s_wifi_event_group == NULL) vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        // 1. ESPERAR TRABAJO (Bloqueado aquí hasta que llegue un comando OTA)
        if (xQueueReceive(s_ota_q, &job, portMAX_DELAY) != pdTRUE) continue;

        // 2. ACTIVAR BLOQUEO DE SERVICIOS
        g_ota_en_progreso = true; 
        write_system_log(TAG, "OTA RECIBIDO: Bloqueando MQTT y WebServer para seguridad.");
        
        // Intentar detener MQTT si ya estaba corriendo por alguna razón
        mqtt_app_stop(); 

        // 3. ESPERAR WIFI (Si se perdió justo antes de empezar)
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        
        ESP_LOGI(TAG, "Iniciando descarga OTA: %s", job.url);
        write_system_log(TAG, "Descargando nuevo firmware...");

        bool otasuccess = false;
        esp_http_client_config_t config = {};
        config.url = job.url;
        config.timeout_ms = 15000;
        config.keep_alive_enable = false;

        if(job.https) { 
            config.crt_bundle_attach = esp_crt_bundle_attach; 
            config.transport_type = HTTP_TRANSPORT_OVER_SSL;
        }

        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        if (esp_http_client_open(client, 0) != ESP_OK) {
            ESP_LOGE(TAG, "Error: No se pudo abrir conexión con el servidor OTA");
            write_system_log(TAG, "Error de conexión servidor OTA.");
            esp_http_client_cleanup(client);
            g_ota_en_progreso = false; // Liberar para que el sistema siga vivo
            continue; 
        }

        esp_http_client_fetch_headers(client);
        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
        esp_ota_handle_t update_handle = 0;
        
        // Buffer de 2KB para los fragmentos
        char *upgrade_data_buf = (char *)malloc(2048);
        if (!upgrade_data_buf) {
            ESP_LOGE(TAG, "No hay RAM suficiente para el buffer OTA");
            esp_http_client_cleanup(client);
            g_ota_en_progreso = false;
            continue;
        }

        esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);

        int binary_size = 0;
        bool header_ok = false;

        // 4. BUCLE DE ESCRITURA EN FLASH
        while (1) {
            int data_read = esp_http_client_read(client, upgrade_data_buf, 2048);
            
            if (data_read < 0) {
                ESP_LOGE(TAG, "Conexión interrumpida durante descarga");
                break; 
            }
            if (data_read == 0) {
                otasuccess = true;
                break;
            }

            // Validación mínima del primer byte (ESP32 Magic Byte)
            if (!header_ok) {
                if ((uint8_t)upgrade_data_buf[0] != 0xE9) {
                    ESP_LOGE(TAG, "Error: El archivo no es un binario válido de ESP32");
                    break;
                }
                header_ok = true;
            }

            esp_ota_write(update_handle, (const void *)upgrade_data_buf, data_read);
            binary_size += data_read;
            
            // Log cada ~100KB para no saturar
            if (binary_size % (1024 * 100) == 0) {
                ESP_LOGD(TAG, "Escrito: %d bytes", binary_size);
            }
        }

        // 5. CIERRE Y LIMPIEZA
        free(upgrade_data_buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (otasuccess) {
            esp_err_t err = esp_ota_end(update_handle);
            if (err == ESP_OK) {
                esp_ota_set_boot_partition(update_partition);
                write_system_log(TAG, "OTA Finalizado con éxito. Reiniciando...");
                ESP_LOGI(TAG, "Reinicio en 1 segundo...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            } else {
                ESP_LOGE(TAG, "Error en validación final (ota_end): %s", esp_err_to_name(err));
            }
        } else {
            esp_ota_abort(update_handle);
            write_system_log(TAG, "OTA Fallido: El firmware no se aplicó.");
        }

        // Si falló y llegó aquí, liberamos el bloqueo para que MQTT/Web vuelvan a funcionar
        g_ota_en_progreso = false; 
        ESP_LOGI(TAG, "Esperando siguiente trabajo OTA...");
    }
}*/
// -----------------------------------------------------------------------------
// API pública
// -----------------------------------------------------------------------------

bool ota_worker_start(void) {
    s_ota_q = xQueueCreate(OTA_QUEUE_LENGTH, sizeof(ota_job_t));
    if (!s_ota_q) return false;

    return (xTaskCreate(ota_worker_task, "ota_worker", 8192, NULL, 10, NULL) == pdPASS); //ampliamos el stack a 8192 porque vamos a usar https y prioridad a 10
}

bool ota_submit(const ota_job_t *job) {
    if (!s_ota_q || !job) return false;

    // ✅ Copiar el struct antes de enviarlo a la cola
    ota_job_t copy = *job;
    return xQueueSend(s_ota_q, &copy, pdMS_TO_TICKS(200)) == pdTRUE;
}