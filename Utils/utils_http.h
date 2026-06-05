#ifndef UTILS_HTTP_H
#define UTILS_HTTP_H
#include <stdio.h>
#include "esp_http_client.h" // Se usa para el tipo esp_http_client_handle_t
#include "esp_err.h"
#include <string>
#include <vector>
//la siguiente es C++
std::string http_get(const std::string& url); // Función de conveniencia para GET que devuelve un std::string, hasta 32K
std::string http_get_dynamic(const std::string& url, size_t max_size = 32768); // Versión alternativa que permite especificar el tamaño máximo de la respuesta
#ifdef __cplusplus

        extern "C"  {
    #endif



// Definición para el tamaño máximo de la respuesta HTTP
#define MAX_HTTP_OUTPUT_BUFFER 2048



/**
 * @brief Realiza una petición HTTP GET.
 * * @param url La URL completa a la que se debe acceder (ej: "http://example.com/data").
 * @param response_buffer Buffer donde se almacenará el cuerpo de la respuesta.
 * @param max_len Tamaño máximo del response_buffer.
 * @return esp_err_t ESP_OK si la petición fue exitosa y la respuesta fue copiada.
 */
esp_err_t http_perform_get(const char *url, char *response_buffer, size_t max_len);

/**
 * @brief Realiza una petición HTTP POST con un cuerpo JSON.
 * * @param url La URL completa a la que se debe enviar la petición.
 * @param json_payload El cuerpo JSON a enviar.
 * @param response_buffer Buffer donde se almacenará el cuerpo de la respuesta.
 * @param max_len Tamaño máximo del response_buffer.
 * @return esp_err_t ESP_OK si la petición fue exitosa y la respuesta fue copiada.
 */
esp_err_t http_perform_post(const char *url, const char *json_payload, char *response_buffer, size_t max_len, const char* content_type);


 void http_test_task(void *pvParameters);
 esp_err_t http_perform_request(
    const char *url, 
    esp_http_client_method_t method, 
    const char *payload, 
    char *response_buffer, 
    size_t max_len, 
    const char* content_type = "application/x-www-form-urlencoded");


 #ifdef __cplusplus
        }
    #endif
#endif // UTILS_HTTP_H