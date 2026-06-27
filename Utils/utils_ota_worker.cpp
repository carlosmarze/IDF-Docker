#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_http_client.h" //para el envío del resultado OTA al server
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

static void send_ota_result(const char* job_url, bool success)
{
    std::string url(job_url);

    // 1) Reemplazar SOLO el path "/update" por "/updateresult"
    //    Todo lo que está después (el ?VERSION=...&SensorID=...&MAC=...) queda intacto.
    const char* needle = "/update";
    size_t pos = url.find(needle);
    if (pos == std::string::npos) {
        ESP_LOGE("OTA_RESULT", "URL OTA inválida: %s", job_url);
        return;
    }

    url.replace(pos, strlen(needle), "/updateresult");

    // 2) Agregar &Result=OK/FAIL al final, sin tocar nada de lo anterior
    url +=  "&Result=";
    url += success ? "OK" : "FAIL";

    ESP_LOGI("OTA_RESULT", "URL resultado OTA: %s", url.c_str());

    esp_http_client_config_t config = {};
    config.event_handler = http_event_handler; 
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 5000;
    config.skip_cert_common_name_check = true;
    config.cert_pem = NULL;


    esp_http_client_handle_t clienthttp = esp_http_client_init(&config);
    if (!clienthttp) return;

    esp_err_t err = esp_http_client_perform(clienthttp);
    if (err == ESP_OK) {
        ESP_LOGI("OTA_RESULT", "Resultado enviado. HTTP %d",
                 esp_http_client_get_status_code(clienthttp));
    } else {
        ESP_LOGE("OTA_RESULT", "Error enviando resultado: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(clienthttp);
}



// -----------------------------------------------------------------------------
// Tarea principal OTA
// -----------------------------------------------------------------------------

static void ota_worker_task(void *arg) {
    
    //std::string log_msg = "";
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
            LOGE(TAG, "Error: No se pudo abrir conexión con el servidor OTA");
            //write_system_log(TAG, log_msg.c_str());
            //ESP_LOGE(TAG, "%s", log_msg.c_str());
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
                    
                    LOGW(TAG, "Servidor respondió: %s", upgrade_data_buf);
                    //write_system_log(TAG, log_msg.c_str());
                    //ESP_LOGW(TAG,  "%s", log_msg.c_str());
                   
                    break; // Salimos del bucle sin haber llamado a esp_ota_begin
                }
                
                // Si llegamos aquí, ES un binario. Ahora sí, preparamos el sistema:
                //g_ota_en_progreso = true; // <-- Marcamos que hay una OTA en progreso para que otras tareas lo sepan y actúen en consecuencia (ej: no reconectar WiFi, no intentar MQTT, etc)
                LOGI(TAG, "Binario detectado. Iniciando escritura ");
                //write_system_log(TAG, log_msg.c_str());
                //ESP_LOGI(TAG,  "%s", log_msg.c_str());

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
                send_ota_result(job.url, true); // Enviar resultado OK al server
                esp_ota_set_boot_partition(update_partition);
                LOGI(TAG, "\n\n\nOTA Ok. Reiniciando..\n\n"); 
                //write_system_log(TAG, log_msg.c_str());
                //ESP_LOGI(TAG,  "%s", log_msg.c_str());
                //ESP_LOGI(TAG, "OTA Ok. Reiniciando...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
        } else {
            if (ota_started) {
                esp_ota_abort(update_handle);
                LOGW(TAG, "\nOTA FAIL!!\n"); 
                //write_system_log(TAG, log_msg.c_str());
                //ESP_LOGW(TAG,  "%s", log_msg.c_str());
                send_ota_result(job.url,  false); // Enviar resultado FAIL al server
            }
            // Si no llegamos a empezar el OTA, simplemente volvemos a la normalidad

            g_ota_en_progreso = false;
            // IMPORTANTE: Si detuvimos el MQTT, hay que volver a arrancarlo
            
            
            ESP_LOGI(TAG, "rearrancando MQTT");
            mqtt_app_start();
        }
        
        LOGI(TAG, "Ciclo OTA terminado. Esperando..."); 
        
        //write_system_log(TAG, log_msg.c_str());
        //ESP_LOGI(TAG,  "%s", log_msg.c_str());
        //ESP_LOGI(TAG, "Ciclo OTA terminado. Esperando...");
    }
}

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


