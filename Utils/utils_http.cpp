#include "utils_http.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils_logger.h"
#include "utils_events.h"
#include "utils_wifi.h"

static const char *TAG = "HTTP_CLIENT";

// --- Variables para capturar la respuesta del servidor ---
// Usamos variables estáticas para que solo la función de manejo de eventos las vea.
// Si las usáramos en múltiples peticiones concurrentes, tendrían que ser thread-safe.

static char *global_response_buffer = NULL;
static size_t global_max_len = 0;
static size_t current_data_read = 0;
static size_t ignored_data_size = 0; // NUEVA VARIABLE para rastrear datos ignorados

/**
 * @brief Manejador de eventos para el cliente HTTP de ESP-IDF.
 * * Captura y almacena el cuerpo de la respuesta HTTP.
 */

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    log_event_t levt; //nro de evento a loggear en la cola global del logger
    bool log_event = false; // Si el evento es relevante para loggear (ej: desconexiones, conexiones, etc.)
    
    switch (evt->event_id) {

    case HTTP_EVENT_ERROR:
        //write_system_log("HTTP", "HTTP_EVENT_ERROR");
        levt = LOG_EVT_HTTP_ERROR;
        log_event = true;
        break;

    case HTTP_EVENT_ON_CONNECTED:
        //write_system_log("HTTP", "HTTP_EVENT_ON_CONNECTED");
        ESP_LOGI("HTTP", "CONNECTED: %s = %s",
                 evt->header_key ? evt->header_key : "",
                 evt->header_value ? evt->header_value : "");
        //write_system_log("HTTP", "HTTP_EVENT_ON_CONNECTED");
        //levt = LOG_EVT_HTTP_CONNECTED;
        //log_event = true;
        break;

    case HTTP_EVENT_HEADERS_SENT:
        ESP_LOGI("HTTP", "HTTP_EVENT_HEADERS_SENT");
        break;

    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI("HTTP", "HEADER: %s = %s",
                 evt->header_key ? evt->header_key : "",
                 evt->header_value ? evt->header_value : "");
        break;

    case HTTP_EVENT_ON_HEADERS_COMPLETE:
        ESP_LOGI("HTTP", "HEADER Complete: %s = %s",
                 evt->header_key ? evt->header_key : "",
                 evt->header_value ? evt->header_value : "");
        break;

    case HTTP_EVENT_ON_STATUS_CODE:
        ESP_LOGI("HTTP", "Status Code: %s = %s",
                 evt->header_key ? evt->header_key : "",
                 evt->header_value ? evt->header_value : "");
        break;

    case HTTP_EVENT_ON_DATA:
        if (global_response_buffer != NULL && global_max_len > 0) {

            size_t remaining_space = global_max_len - 1 - current_data_read;
            size_t copy_len = evt->data_len < remaining_space ? evt->data_len : remaining_space;

            if (copy_len > 0) {
                memcpy(global_response_buffer + current_data_read, evt->data, copy_len);
                current_data_read += copy_len;
                global_response_buffer[current_data_read] = '\0';
            }
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI("HTTP", "HTTP_EVENT_ON_FINISH");
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGW("HTTP", "HTTP_EVENT_DISCONNECTED");
        break;

    default:
        ESP_LOGW("HTTP", "Evento HTTP desconocido id=%d", evt->event_id);
        break;
    }//fin del switch

    if (log_event) {
        //write_system_log(TAG, log_msg.c_str());
        xQueueSend(g_log_queue, &levt, portMAX_DELAY);
        //ESP_LOGI(TAG, "%s", log_msg.c_str()); 
    }

    return ESP_OK;
}


 

/**
 * @brief Función base que configura y ejecuta la petición HTTP.
 */
esp_err_t http_perform_request(
    const char *url, 
    esp_http_client_method_t method, 
    const char *payload, 
    char *response_buffer, 
    size_t max_len, 
    const char* content_type) {
    std::string log_msg = "";
    // 1. Configuración de la estructura del cliente HTTP
   global_response_buffer = response_buffer;
   //printf("HTTP REQ content_type: %s\n", content_type);
   // Limpieza inicial: Poner el primer caracter en nulo
   if (!wifi_ready) {
        log_msg = "No se puede realizar la petición HTTP: Wi-Fi no está listo.";
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGE(TAG, "%s", log_msg.c_str());
        return ESP_FAIL;
    }
    
   if (response_buffer != NULL && max_len > 0) {
        response_buffer[0] = '\0'; 
    }
    esp_http_client_config_t config = {}; // Inicializa todo a 0/NULL automáticamente

    // Asignamos los campos explícitamente (el orden ya no importa)
    config.url = url;
    config.event_handler = _http_event_handler;
    config.user_data = NULL; // Opcional, ya es NULL por el {}
    config.disable_auto_redirect = true;
    config.timeout_ms = 5000; // Darle 5 segundos antes de rendirse
    config.user_agent = "ESP32_Client/1.0"; // Identificarse correctamente

    // Ahora inicializas el cliente con la config ya llena
        
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        log_msg = "Fallo al inicializar el cliente HTTP";
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGE(TAG, "%s", log_msg.c_str());
        return ESP_FAIL;
    }

    // 2. Preparar el buffer de respuesta global
    global_response_buffer = response_buffer;
    global_max_len = max_len;
    current_data_read = 0;
    
    // Limpiar el buffer de destino
    if (response_buffer != NULL && max_len > 0) {
        response_buffer[0] = '\0';
    }

    // 3. Configuración de Petición (Método y Payload)
    esp_http_client_set_method(client, method);

    if (method == HTTP_METHOD_POST && payload != NULL) {
        // Establecer el tipo de contenido como JSON
        esp_http_client_set_header(client, "Content-Type", content_type);
        // Establecer el cuerpo (payload)
        esp_http_client_set_post_field(client, payload, strlen(payload));
    }
    
    // 4. Ejecutar la petición
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        log_msg = "URL: " + std::string(url) + ", Método: " + (method == HTTP_METHOD_GET ? "GET" : "POST") + 
                  ", Código de Estado HTTP: " + std::to_string(status_code) + ", Total leido: " + std::to_string(current_data_read) + ", Total ignorado: " + std::to_string(ignored_data_size);
        //write_system_log(TAG, log_msg.c_str()); Llena mucho el log, lo dejamos solo en ESP_LOGI. Hay que pensar en dos archivos de log, uno para eventos normales y 
        //otro para debug detallado o eventos corrientes.

        ESP_LOGI(TAG, "%s", log_msg.c_str());

        //ESP_LOGI(TAG, "URL: %s", url);
        //ESP_LOGI(TAG, "Método: %s, Código de Estado HTTP: %d", (method == HTTP_METHOD_GET ? "GET" : "POST"), status_code);
        
        if (status_code >= 200 && status_code < 300) {
            // Éxito: El cuerpo de la respuesta ya fue capturado en _http_event_handler
            err = ESP_OK;
        } else {
            log_msg = "La petición falló con el código de estado HTTP: " + std::to_string(status_code);
            write_system_log(TAG, log_msg.c_str());
            ESP_LOGE(TAG, "%s", log_msg.c_str());
            //ESP_LOGE(TAG, "La petición falló con el código de estado HTTP: %d", status_code);
            err = ESP_FAIL;
        }
    } else {
        log_msg = "Error al realizar la petición HTTP: " + std::string(esp_err_to_name(err));
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGE(TAG, "%s", log_msg.c_str());
        //ESP_LOGE(TAG, "Error al realizar la petición HTTP: %s", esp_err_to_name(err));
    }

    // 5. Limpieza
    esp_http_client_cleanup(client);
    
    // Resetear las variables globales (por seguridad)
    global_response_buffer = NULL;
    global_max_len = 0;
    current_data_read = 0;

    return err;
}

// --- Implementaciones públicas ---

esp_err_t http_perform_get(const char *url, char *response_buffer, size_t max_len) {
    return http_perform_request(url, HTTP_METHOD_GET, NULL, response_buffer, max_len);
}

esp_err_t http_perform_post(const char *url, const char *json_payload, char *response_buffer, size_t max_len, const char* content_type) {
    //printf("HTTP POST content_type: %s\n", content_type);
    return http_perform_request(url, HTTP_METHOD_POST, json_payload, response_buffer, max_len, content_type);
}

std::string http_get(const std::string& url)
{
    std::vector<char> buf(32768); // 32 KB
    esp_err_t err = http_perform_get(url.c_str(), buf.data(), buf.size());

    if (err != ESP_OK) return "";

    return std::string(buf.data());
}
