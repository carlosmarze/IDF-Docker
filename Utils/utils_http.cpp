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

 //Propuesta de Qwen Studio: En lugar de usar variables globales, podríamos usar el campo user_data del cliente HTTP para pasar un puntero a una estructura de contexto que contenga el buffer de respuesta, su tamaño máximo, la cantidad de datos leídos hasta ahora, y la cantidad de datos ignorados. Esto haría que el manejador de eventos sea reentrante y seguro para múltiples peticiones concurrentes, sin riesgo de colisiones en las variables globales. Además, podríamos agregar más logs dentro del manejador de eventos para tener un registro detallado de cada paso del proceso HTTP, lo cual facilitaría la depuración y el monitoreo del sistema.
// Estructura para pasar contexto al event handler (sin variables globales)
typedef struct {
    char *buffer;
    size_t max_len;
    size_t current_read;
    size_t ignored_size;
} http_context_t;


// Event handler reentrante (usa user_data en lugar de globales)
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    log_event_t levt; 
    bool log_event = false; 
    
    // 🔥 OBTENER EL CONTEXTO PASADO DESDE http_perform_request
    http_context_t *ctx = (http_context_t *)evt->user_data;
    
    switch (evt->event_id) {

    case HTTP_EVENT_ERROR:
        levt = LOG_EVT_HTTP_ERROR;
        log_event = true;
        break;

    case HTTP_EVENT_ON_CONNECTED:
        // 🔥 CORREGIDO: No hay cabeceras todavía, solo avisar que conectó.
         ESP_LOGI(TAG, "CONNECTED"); 
        break;

    case HTTP_EVENT_HEADERS_SENT:
         ESP_LOGI(TAG, "HTTP_EVENT_HEADERS_SENT");
        break;

    case HTTP_EVENT_ON_HEADER:
         ESP_LOGI(TAG, "HEADER: %s = %s",
                 evt->header_key ? evt->header_key : "",
                 evt->header_value ? evt->header_value : "");
        break;

    case HTTP_EVENT_ON_HEADERS_COMPLETE:
        // 🔥 CORREGIDO: Los punteros ya son inválidos, no los imprimas.
         ESP_LOGI(TAG, "HEADER Complete (Todas las cabeceras recibidas)");
        break;

    case HTTP_EVENT_ON_STATUS_CODE:
        // 🔥 CORREGIDO: El código de estado se obtiene de otra forma, no de header_key.
        // Si quieres loguearlo, usa esp_http_client_get_status_code(evt->client)
         ESP_LOGI(TAG, "Status Code received: %d", esp_http_client_get_status_code(evt->client));
        break;

    case HTTP_EVENT_ON_DATA:
        // 🔥 USAR EL CONTEXTO EN LUGAR DE LAS GLOBALES
        if (ctx != NULL && ctx->buffer != NULL && ctx->max_len > 0) {
            
            // Buena práctica: verificar que no sea chunked (si usas chunked, la lógica cambia)
            if (!esp_http_client_is_chunked_response(evt->client)) {
                
                size_t remaining_space = ctx->max_len - 1 - ctx->current_read;
                size_t copy_len = evt->data_len < remaining_space ? evt->data_len : remaining_space;

                if (copy_len > 0) {
                    memcpy(ctx->buffer + ctx->current_read, evt->data, copy_len);
                    ctx->current_read += copy_len;
                    ctx->buffer[ctx->current_read] = '\0'; // Null-terminate
                }
                
                // 🔥 Acumular datos ignorados si el buffer se llenó (mantienes tu métrica)
                if (evt->data_len > remaining_space) {
                    ctx->ignored_size += (evt->data_len - remaining_space);
                }
            }
        }
        break;

    case HTTP_EVENT_ON_FINISH:
         ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        break;

    case HTTP_EVENT_DISCONNECTED:
         ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED");
        break;

    default:
         ESP_LOGW(TAG, "Evento HTTP desconocido id=%d", evt->event_id);
        break;
    }

    if (log_event) {
        xQueueSend(g_log_queue, &levt, portMAX_DELAY);
    }

    return ESP_OK;
}

esp_err_t http_perform_request(
    const char *url, 
    esp_http_client_method_t method, 
    const char *payload, 
    char *response_buffer, 
    size_t max_len, 
    const char* content_type) {
    
    // Validación de entrada
    if (!url) {
        ESP_LOGE(TAG, "URL es NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!wifi_ready) {
        ESP_LOGE(TAG, "No se puede realizar la petición HTTP: Wi-Fi no está listo.");
        return ESP_FAIL;
    }

    // Contexto local (en el stack, pero solo un puntero pequeño)
    http_context_t ctx = {
        .buffer = response_buffer,
        .max_len = (response_buffer && max_len > 0) ? max_len : 0,
        .current_read = 0,
        .ignored_size = 0
    };

    // Limpiar buffer de respuesta
    if (response_buffer && max_len > 0) {
        response_buffer[0] = '\0';
    }

    // Configuración del cliente HTTP
        // 🔥 INICIALIZACIÓN SEGURA (No depende del orden de los campos en IDF)
    esp_http_client_config_t config = {}; // Inicializa todo a 0/NULL automáticamente

    config.url = url;
    config.event_handler = _http_event_handler;
    config.user_data = &ctx;              // <-- 🔥 Pasamos el contexto local
    config.disable_auto_redirect = true;
    config.timeout_ms = 5000; 
    config.user_agent = "ESP32_Client/1.0"; 

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Fallo al inicializar el cliente HTTP");
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, method);

    if (method == HTTP_METHOD_POST && payload) {
        esp_http_client_set_header(client, "Content-Type", content_type ? content_type : "application/json");
        esp_http_client_set_post_field(client, payload, strlen(payload));
    }
    
    // Ejecutar la petición
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "URL: %s, Método: %s, HTTP: %d, Leído: %u, Ignorado: %u", 
                 url, 
                 (method == HTTP_METHOD_GET ? "GET" : "POST"), 
                 status_code, 
                 (unsigned int)ctx.current_read, 
                 (unsigned int)ctx.ignored_size);

        if (status_code >= 200 && status_code < 300) {
            err = ESP_OK;
        } else {
            ESP_LOGE(TAG, "La petición falló con código HTTP: %d", status_code);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Error HTTP: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// --- Funciones públicas ---

esp_err_t http_perform_get(const char *url, char *response_buffer, size_t max_len) {
    return http_perform_request(url, HTTP_METHOD_GET, NULL, response_buffer, max_len, NULL);
}

esp_err_t http_perform_post(const char *url, const char *json_payload, char *response_buffer, size_t max_len, const char* content_type) {
    return http_perform_request(url, HTTP_METHOD_POST, json_payload, response_buffer, max_len, content_type);
}

// Versión mejorada de http_get (evita buffer gigante en el stack)
std::string http_get(const std::string& url) {
    if (url.empty()) return "";
    
    // Buffer dinámico en el heap (no en el stack)
    std::vector<char> buf(8192); // 8 KB es suficiente para la mayoría de respuestas
    
    esp_err_t err = http_perform_get(url.c_str(), buf.data(), buf.size());
    if (err != ESP_OK) return "";
    
    return std::string(buf.data());
}

// Versión alternativa con buffer dinámico (para respuestas grandes)
std::string http_get_dynamic(const std::string& url, size_t max_size) {
    if (url.empty() || max_size == 0) return "";
    
    // Asignar en el heap (no en el stack)
    char *buffer = (char *)malloc(max_size);
    if (!buffer) {
        ESP_LOGE(TAG, "No se pudo asignar memoria para HTTP GET");
        return "";
    }
    
    esp_err_t err = http_perform_get(url.c_str(), buffer, max_size);
    std::string result;
    
    if (err == ESP_OK) {
        result = std::string(buffer);
    }
    
    free(buffer);
    return result;
}
