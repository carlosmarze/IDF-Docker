#include "utils_webs.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
//#include "esp_http_server.h" //ya incluido en utils_webs.h
#include "esp_littlefs.h"
#include "mbedtls/base64.h"
#include "driver/gpio.h" // Necesario si controlas GPIOs aquí
#include <time.h>

#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <string>


#include "utils_time.h" // <--- para funciones de fecha y hora
#include "utils_files.h" // Para MOUNT_POINT
#include <ctype.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <dirent.h>      // Para opendir, readdir, closedir

#include "utils_cmd_dispatcher.h"
#include "utils_cmd_processor.h"
#include "utils_logger.h"
#include "utils_events.h"
#include "utils_config.h" // Para g_ota_en_progreso
#include "utils_wifi.h" // Para funciones de Wi-Fi

//extern CommandDispatcher dispatcher;

static char current_ip[16]; // Buffer para la IP del último cliente. 
static const char *TAG = "UTILS_SERVER";
//static httpd_handle_t server_handle = NULL;
static httpd_handle_t server = NULL;
static int current_token = 0; // Token numérico aleatorio
static char authorized_ip[16] = {0}; // Aquí guardamos la IP autorizada

//tamaño del buffer de una respuesta HTML dinámica
#define HTML_BUFFER_SIZE 256
// Credenciales
//#define WEB_USER "admin"
//#define WEB_PASS "1234"

// 
/* ====================================================
   --- OBTENER IP DEL CLIENTE ---
   ==================================================== */
static void get_client_ip(httpd_req_t *req, char *ip_str) {
    // Inicializar por defecto para evitar basura si falla
    strcpy(ip_str, "0.0.0.0");
    
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "Error: No se pudo obtener el FD del socket");
        return;
    }

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) == 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)&addr;
            inet_ntoa_r(s->sin_addr, ip_str, 16);
        }
    } else {
        ESP_LOGW(TAG, "getpeername falló en FD %d", sockfd);
    }
}

/* ====================================================
   LÓGICA DE HARDWARE (FUNCIONES DISPARADAS)
   ==================================================== */
// Puedes mover esto a otro archivo (ej: hardware.c) si crece mucho
static void accion_hardware_on() {
    ESP_LOGI(TAG, ">>> ACCION: ENCENDER RECURSO <<<");
    // gpio_set_level(GPIO_NUM_2, 1);
}

static void accion_hardware_off() {
    ESP_LOGI(TAG, ">>> ACCION: APAGAR RECURSO <<<");
    // gpio_set_level(GPIO_NUM_2, 0);
}

/* ====================================================
   AUTENTICACIÓN
   ==================================================== */
// --- AUTENTICACIÓN HÍBRIDA ---
static bool is_authenticated_Ip(httpd_req_t *req) {
    char current_ip[16];
    get_client_ip(req, current_ip);

    // 1. Si la IP ya está autorizada, saltamos el login
    if (strlen(authorized_ip) > 0 && strcmp(current_ip, authorized_ip) == 0) {
        return true;
    }

    // 2. Si no, pedimos User/Pass (Autenticación Básica)
    char head[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", head, sizeof(head)) == ESP_OK) {
        char *base64 = strchr(head, ' ');
        if (base64) {
            base64++;
            unsigned char output[64];
            size_t out_len;
            mbedtls_base64_decode(output, sizeof(output), &out_len, (unsigned char *)base64, strlen(base64));
            output[out_len] = '\0';
            
            char expected[64];
            snprintf(expected, sizeof(expected), "%s:%s", webuser, webpass);
            
            if (strcmp((char *)output, expected) == 0) {
                // ÉXITO: Guardamos esta IP como autorizada
                strncpy(authorized_ip, current_ip, 16);
                ESP_LOGI(TAG, "Usuario autenticado. IP %s registrada.", authorized_ip);
                return true;
            }
        }
    }

    // 3. Fallo: Pedir credenciales
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32 Control\"");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Acceso Restringido");
    return false;
}
/* ====================================================
   AUTENTICACIÓN básica
   ==================================================== */
static bool is_authenticated(httpd_req_t *req) {
    char head[128];
    // Buscamos el header "Authorization"
    if (httpd_req_get_hdr_value_str(req, "Authorization", head, sizeof(head)) == ESP_OK) {
        char *base64 = strchr(head, ' ');
        if (base64) {
            base64++;
            unsigned char output[64];
            size_t out_len;
            mbedtls_base64_decode(output, sizeof(output), &out_len, (unsigned char *)base64, strlen(base64));
            output[out_len] = '\0';

            char expected[64];
            snprintf(expected, sizeof(expected), "%s:%s", webuser, webpass);
            
            if (strcmp((char *)output, expected) == 0) return true;
        }
    }
    // Si no coincide o no existe, pedimos credenciales (Pop-up del navegador)
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32 Control\"");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Acceso Requerido");
    return false;
}

/* ====================================================
   HANDLERS
   ==================================================== */


static esp_err_t cmd_on_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) return ESP_OK;
    
    accion_hardware_on(); // <--- Dispara función

    // --- 1. Obtener fecha usando tu nueva función --- Lo pasamos a create_status_page
    //char str_fecha[64];
    //get_fecha_hora(str_fecha, sizeof(str_fecha));

    // --- 2. Armar el HTML ---
    char resp_html[HTML_BUFFER_SIZE];
    // 💡 Invocamos la función, la cual llena nuestro buffer
    create_status_page("ON", resp_html, sizeof(resp_html));

    httpd_resp_send(req, resp_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t cmd_off_handler(httpd_req_t *req) {
    if (!is_authenticated(req)) return ESP_OK;
    
    accion_hardware_off(); // <--- Dispara función
    
    // --- 2. Armar el HTML ---
    char resp_html[HTML_BUFFER_SIZE];
    // 💡 Invocamos la función, la cual llena nuestro buffer
    create_status_page("OFF", resp_html, sizeof(resp_html));
    httpd_resp_send(req, resp_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
/* ====================================================
   crear respuesta dinámica
   ==================================================== */

static void create_status_page(const char *status_cmd, char *buffer, size_t max_len) {
    
    // 1. Obtener la hora formateada
    char str_fecha[64];
    get_fecha_hora(str_fecha, sizeof(str_fecha));
    
    // 2. Determinar el mensaje y el estado
    const char *msg_title = (strcmp(status_cmd, "ON") == 0) ? "ENCENDIDO OK" : "APAGADO OK";
    const char *msg_state = (strcmp(status_cmd, "ON") == 0) ? "ENCENDIDO" : "APAGADO";

    // 3. Armar el HTML dinámico
    snprintf(buffer, max_len, 
             "<html><body>"
             "<p>Fecha: %s</p>"
             "</body></html>", 
             str_fecha);
    /*snprintf(buffer, max_len, 
             "<html><body>"
             "<h1>%s</h1>"
             "<p>Estado: <b>%s</b></p>"
             "<p>Fecha: %s</p>"
             "<a href='/'>Volver</a>"
             "</body></html>", 
             msg_title, msg_state, str_fecha);*/
}


/* ====================================================
   INICIALIZACIÓN PÚBLICA
   ==================================================== */
// Declaración adelantada de handlers que están abajo de start_webserver
static esp_err_t http_more_handler(httpd_req_t *req);
static esp_err_t http_ls_handler(httpd_req_t *req);
static esp_err_t cmd_handler(httpd_req_t *req);
static esp_err_t index_handler(httpd_req_t *req);
static esp_err_t ws_handler(httpd_req_t *req);
static esp_err_t logout_handler(httpd_req_t *req);
static esp_err_t download_handler(httpd_req_t *req);
static esp_err_t http_rm_handler(httpd_req_t *req);
static esp_err_t upload_handler(httpd_req_t *req);
static esp_err_t upload_form_handler(httpd_req_t *req);


//Funciones para webupdate, pero no las estoy usando, por ahora, lo dejo para más adelante

httpd_handle_t global_webserver_handle = NULL;

void webserver_task(void *arg)
{
    while (true) {

        // Esperar WiFi conectado
        xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE, pdFALSE,
            portMAX_DELAY
        );

        ESP_LOGI("WEBSRV", "WiFi listo, iniciando servidor...");
        start_webserver(global_dispatcher_ptr);

        // Esperar caída del WiFi
        xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_FAIL_BIT,
            pdFALSE, pdFALSE,
            portMAX_DELAY
        );

        ESP_LOGW("WEBSRV", "WiFi caído, deteniendo servidor...");
        stop_webserver();

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


bool server_running() {
    return global_webserver_handle != NULL;
}

void stop_webserverOLD() {
    if (global_webserver_handle) {
        httpd_stop(global_webserver_handle);
        global_webserver_handle = NULL;
    }
}

void stop_webserver()
{
    if (global_webserver_handle == NULL) {
        LOGW(TAG, "Servidor HTTP ya estaba detenido");
        return;
    }

    LOGI(TAG, "Deteniendo servidor HTTP...");

    esp_err_t err = httpd_stop(server);

    if (err == ESP_OK) {
        LOGI(TAG, "Servidor HTTP detenido correctamente");
        global_webserver_handle = NULL;
    } else {
        LOGE(TAG, "Error al detener servidor HTTP: %s", esp_err_to_name(err));
    }

    server = NULL;
}


void restart_webserver(CommandDispatcher* disp_ptr)
{
    ESP_LOGW(TAG, "Reiniciando servidor HTTP...");

    stop_webserver();
    vTaskDelay(pdMS_TO_TICKS(200));   // pequeña pausa para liberar sockets

    start_webserver(disp_ptr);
}


void start_webserver(CommandDispatcher* disp_ptr) {
    global_webserver_handle = server;
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096;        // WebSocket seguro
    config.lru_purge_enable = true; // Cierra conexiones viejas si se llena
    //Keep alive settings
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;      // 5 segundos de idle antes de enviar keep-alive
    config.keep_alive_interval = 2;  // 2 segundos entre pings
    config.keep_alive_count = 2;     // Si falla 3 veces, cierra el socket
    // ------------------
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_open_sockets = 4; // Aumentar si es necesario
    config.max_uri_handlers = 15; // Aumentar si es necesario cuántas urls manejará (/ls /rm /more /cmd /ws etc)
    // REDUCE estos tiempos. Si Chrome no responde en 3 seg, que se libere el socket.
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;

    if (httpd_start(&server, &config) == ESP_OK) {
        if (disp_ptr) {
            disp_ptr->setWebServer(server);
            ESP_LOGI(TAG, "Dispatcher conectado al servidor Web");
        }

        httpd_uri_t uri_root = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL,
            .is_websocket = false //Requerido cuando habilitamos websockets en el IDF
        };
        
        httpd_uri_t uri_on = {
            .uri = "/cmd/on",
            .method = HTTP_GET,
            .handler = cmd_on_handler,
            .user_ctx = NULL,
            .is_websocket = false //Requerido cuando habilitamos websockets en el IDF
        };
        httpd_uri_t uri_off = {
            .uri = "/cmd/off",
            .method = HTTP_GET,
            .handler = cmd_off_handler,
            .user_ctx = NULL,
            .is_websocket = false //Requerido cuando habilitamos websockets en el IDF
        };
        httpd_uri_t uri_cmd = {
            .uri = "/cmd*",
            .method = HTTP_GET,
            .handler = cmd_handler,
            .user_ctx = NULL,
            .is_websocket = false //Requerido cuando habilitamos websockets en el IDF
        };
        // URI para WebSocket
        httpd_uri_t uri_ws = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true // <--- Esto es clave
        };
        //logout
        httpd_uri_t uri_logout = { 
            .uri = "/logout", 
            .method = HTTP_GET, 
            .handler = logout_handler,
            .is_websocket = false  //Requerido cuando habilitamos websockets en el IDF
        };
        //downlad archivos: 
        httpd_uri_t uri_download = {
            .uri       = "/download",  // <--- Esta es la ruta que pones en el botón
            .method    = HTTP_GET,
            .handler   = download_handler, // <--- El nombre de tu función
            .user_ctx  = NULL
        };
        // Handler GET: muestra formulario
        httpd_uri_t uri_upload_form = {
            .uri       = "/upload",
            .method    = HTTP_GET,
            .handler   = upload_form_handler,
            .user_ctx  = NULL,
            .is_websocket = false
        };
         httpd_uri_t uri_upload = {
            .uri       = "/upload",  // <---    download = {
            .method    = HTTP_POST,
            .handler   = upload_handler, // <--- El nombre de tu función
            .user_ctx  = NULL
        };
        httpd_uri_t uri_ls = { //ver capaz que conviene hacer cmd?ls=/, y no otro endpoint
            .uri       = "/ls*",
            .method    = HTTP_GET,
            .handler   = http_ls_handler,
            .user_ctx  = NULL
        };

        httpd_uri_t uri_more = { //ver capaz que conviene hacer cmd?more=/log.txt, y no otro endpoint. Habría que generar las funciones more y ls en cmd_set
            .uri       = "/more*",
            .method    = HTTP_GET,
            .handler   = http_more_handler,
            .user_ctx  = NULL
        };
        httpd_uri_t uri_rm = { //ver capaz que conviene hacer cmd?more=/log.txt, y no otro endpoint. Habría que generar las funciones more y ls en cmd_set
            .uri       = "/rm*",
            .method    = HTTP_GET,
            .handler   = http_rm_handler,
            .user_ctx  = NULL
        };

        // Dentro de start_webserver()
        
        
        // Registrar handlers después de iniciar el servidor
        
        httpd_register_uri_handler(server, &uri_ls); //listar archivos
        httpd_register_uri_handler(server, &uri_more); //leer archivo completo
        httpd_register_uri_handler(server, &uri_rm); //borrar archivo 
        
        httpd_register_uri_handler(server, &uri_on);
        httpd_register_uri_handler(server, &uri_off);
        httpd_register_uri_handler(server, &uri_cmd);
        httpd_register_uri_handler(server, &uri_ws); //para WebSocket
        httpd_register_uri_handler(server, &uri_logout);
        httpd_register_uri_handler(server, &uri_download); //para descarga de archivos
        httpd_register_uri_handler(server, &uri_upload_form); //para formulario de subida de archivos GET
        httpd_register_uri_handler(server, &uri_upload); //para subida de archivos POST
        httpd_register_uri_handler(server, &uri_root); // /* debe ir al final para que no pise a los otros 
        

        LOGI(TAG, "Servidor HTTP iniciado en puerto %d", config.server_port);
    } else {
        LOGE(TAG, "Error al iniciar servidor HTTP");
    }
}

//funcion para construir la path a los archivos correctamente
static void build_full_path(char *out, size_t out_size, const char *file_param) {
    // Saltar todos los slashes iniciales
    while (*file_param == '/') file_param++;

    // Construir ruta final: /fs/archivo
    snprintf(out, out_size, "%s/%s", MOUNT_POINT, file_param);
}

// Convierte dos dígitos hex a un valor byte
static int hex2int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Decodifica una URI (ej: "SetSSID=missid%20SetWifiPass=mipass")
// out_len debe contener el tamaño del buffer de salida
// Al final se actualiza con la longitud real escrita
void esp_uri_decode(const char *src, char *dst, size_t *out_len) {
    size_t i = 0;
    while (*src && i + 1 < *out_len) {
        if (*src == '%') {
            int hi = hex2int(*(src + 1));
            int lo = hex2int(*(src + 2));
            if (hi >= 0 && lo >= 0) {
                dst[i++] = (char)((hi << 4) | lo);
                src += 3;
                continue;
            }
        } else if (*src == '+') {
            // Convención: '+' se interpreta como espacio
            dst[i++] = ' ';
            src++;
            continue;
        }
        dst[i++] = *src++;
    }
    dst[i] = '\0';
    *out_len = i;
}

/******************************************************
 * Command handler para /cmd y /cmdx
 * Soporta múltiples pares key=value separados por ' ' o ','
 ****************************************************/
//extern CommandDispatcher dispatcher; // asegúrate de tener tu dispatcher global o accesible
// /cmd?SetSSID%3Dmissid&WifiPass%3dmipass

// cmdx?ota%2Cserver%3Dhttp%3A%2F%2Fserver%2Fupdate.bin%2Creboot%3Dno%2Cchunk%3D2048

static esp_err_t cmd_handler(httpd_req_t *req)
{
    
    if (!is_authenticated(req)) return ESP_OK;

    /*---------------------------------------------------------
     * 1. Determinar modo: /cmd o /cmdx
     *---------------------------------------------------------*/
    bool extendido = false;
    // Obtener el FD del cliente que mandó el mensaje
    int fd = httpd_req_to_sockfd(req);

    if (strncmp(req->uri, "/cmdx", 5) == 0) {
        extendido = true;
    }
    else if (strncmp(req->uri, "/cmd", 4) == 0) {
        extendido = false;
    }
    else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Endpoint desconocido");
        return ESP_OK;
    }

    //char separator = extendido ? ',' : ' ';

    /*---------------------------------------------------------
     * 2. Obtener query string codificada
     *---------------------------------------------------------*/
    char encoded_query[256];
    if (httpd_req_get_url_query_str(req, encoded_query, sizeof(encoded_query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falta query string");
        return ESP_OK;
    }

    printf("Query codificada: %s\n", encoded_query);

    /*---------------------------------------------------------
     * 3. Decodificar query completa
     *---------------------------------------------------------*/
    char query[256] = {0};
    size_t out_len = sizeof(query);

    esp_uri_decode(encoded_query, query, &out_len);

    if (out_len >= sizeof(query))
        query[sizeof(query) - 1] = '\0';
    else
        query[out_len] = '\0';

    printf("Query decodificada: %s\n", query);
    //reemplazar & por espacio si es modo normal
    for (char *p = query; *p; p++) {
        if (*p == '&') {
            *p = ' ';
        }
    }
    printf("Query a comando: %s\n", query);
    //process_commands(dispatcher,CMD_SRC_WEB, "ota,server=http://server/update.bin,reboot=no,chunk=2048", ' ', ',');
    // process_commands(dispatcher, CMD_SRC_WEB,"SetSSID=missid SetWifiPass='esta es mipass' set_led=On", ' ', '=');
    ESP_LOGI(TAG, "Stack libre en web pre proc: %u bytes", uxTaskGetStackHighWaterMark(NULL));
    if (!extendido) { //cmd
        process_commands( CMD_SRC_WEB, query, ' ', '=', fd);
    }
    else { //cmdx
        process_commands( CMD_SRC_WEB, query, ' ', ',', fd);
    }
    ESP_LOGI(TAG, "Stack libre en web post proc: %u bytes", uxTaskGetStackHighWaterMark(NULL));

    /*---------------------------------------------------------
     * 6. Responder al cliente
     *---------------------------------------------------------*/
    char resp_html[HTML_BUFFER_SIZE];
    //create_status_page(cmd.c_str(), resp_html, sizeof(resp_html));
    create_status_page(query, resp_html, sizeof(resp_html));
    //httpd_resp_send(req, resp_html, HTTPD_RESP_USE_STRLEN);
    //ws_broadcast_message(resp_html); // Enviamos la respuesta por WebSocket también Teóricamente, si la saco, la respuesta va a ir solo al que lo pidió

    return ESP_OK;
}


/***************************************************
 * Implementación de WebSockets para que el cliente reciba datos en tiempo real
 ****************************************************/  
// Handler para WebSocket
// --- HANDLER: WEBSOCKET ---
esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        return ESP_OK; // Handshake
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Obtener longitud del frame
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len > 0) {
        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        
        if (ret == ESP_OK) {
            int fd = httpd_req_to_sockfd(req); // <--- CAPTURAMOS EL SOCKET ID
            char* data = (char*)buf;
            // Enviamos al procesador incluyendo el FD para que sepa a quién responder
            ESP_LOGI(TAG, "Stack libre en ws pre proc: %u bytes", uxTaskGetStackHighWaterMark(NULL));
            if (strncmp(data, "X:", 2) == 0) {
                // Formato CMDX: ota, server=http://..., reboot=yes
                // Separador 1: ',' (entre comandos/args)
                // Separador 2: '=' (entre clave y valor)
                process_commands(CMD_SRC_WEB, std::string(data + 2), ' ', ',', fd);
            } else {
                // Formato CMD: SetSSID="mi ssid" SetWifiPass=1234
                // Separador 1: ' ' (espacio entre comandos)
                // Separador 2: '=' (clave=valor)
                process_commands(CMD_SRC_WEB, std::string(data), ' ', '=', fd);
            }
            ESP_LOGI(TAG, "Stack libre en ws post proc: %u bytes", uxTaskGetStackHighWaterMark(NULL));
        }
        free(buf);
    }
    return ret;
}
// --- HANDLER: INDEX HTML ---
esp_err_t index_handler(httpd_req_t *req) {
    // 1. Validar autenticación básica
   if (!is_authenticated(req)) return ESP_FAIL;

    char filepath[500];

    // 2. Si piden "/", elegir indexWS.html si existe, sino indexSet.html
    if (strcmp(req->uri, "/") == 0) {

        char ws_path[500];
        snprintf(ws_path, sizeof(ws_path), "%s/indexWS.html", MOUNT_POINT);

        FILE *test = fopen(ws_path, "r");
        if (test) {
            fclose(test);
            strlcpy(filepath, ws_path, sizeof(filepath));   // usar indexWS.html
        } else {
            snprintf(filepath, sizeof(filepath), "%s/indexSet.html", MOUNT_POINT);
        }

    } else {
        // Cualquier otro archivo se sirve directo
        strlcpy(filepath, MOUNT_POINT, sizeof(filepath));
        strlcat(filepath, req->uri, sizeof(filepath));
    }

    // 3. Abrir el archivo solicitado
    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        char msg[600];
        snprintf(msg, sizeof(msg), "Archivo no encontrado: %s", filepath);
        ESP_LOGE("WEB", "%s", msg);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, msg); // "Archivo no encontrado"
        return ESP_FAIL;
    }

    // 4. Lógica de Token para WebSockets (Se genera en cada petición de archivo)
    current_token = (esp_random() % 9000) + 1000;
    char cookie_str[64];
    snprintf(cookie_str, sizeof(cookie_str), "ws_token=%d; Path=/; Max-Age=60", current_token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie_str);

    // 5. Establecer el Content-Type según la extensión
    if (strstr(filepath, ".html")) httpd_resp_set_type(req, "text/html");
    else if (strstr(filepath, ".css")) httpd_resp_set_type(req, "text/css");
    else if (strstr(filepath, ".js"))  httpd_resp_set_type(req, "application/javascript");
    else if (strstr(filepath, ".png")) httpd_resp_set_type(req, "image/png");
    else if (strstr(filepath, ".ico")) httpd_resp_set_type(req, "image/x-icon");

    // 6. Enviar el archivo en trozos (Streaming)
    char* chunk = (char*)malloc(1024);
    if (!chunk) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes;
    do {
        read_bytes = fread(chunk, 1, 1024, f);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                ESP_LOGE(TAG, "Error enviando trozo de %s", filepath);
                free(chunk);
                fclose(f);
                return ESP_FAIL; // Forzamos el cierre de esta sesión, no intentamos enviar más datos a este cliente
            }
        }
    } while (read_bytes > 0);

    free(chunk);
    fclose(f);
    
    // Finalizar el envío
    httpd_resp_send_chunk(req, NULL, 0);

    // 7. Notificar solo si es un HTML (para no saturar el log con CSS/JS)
    if (strstr(filepath, ".html")) {
        char mensaje_final[700];
        snprintf(mensaje_final, sizeof(mensaje_final), "IP %s accedió a %s", current_ip, req->uri);
        ws_broadcast_message(mensaje_final);
        ESP_LOGI(TAG, "%s", mensaje_final);
    }

    return ESP_OK;
}
/***************************************************
 * enviar respuesta asincrónica con WebSockets, desde cualquier app
 ****************************************************/  
// Función para enviar datos al browser desde cualquier parte del código
// Función de limpieza que el servidor llamará cuando termine de enviar el frame
void ws_async_free_data(void* arg) {
    free(arg);
}

void enviar_respuesta_async(httpd_handle_t server, int fd, const char* msg) {
   if (server == nullptr) {
        ESP_LOGE("WS_DEBUG", "Error: Server Handle es NULL");
        return;
    }
    if (fd <= 0) {
        ESP_LOGE("WS_DEBUG", "Error: FD inválido (%d)", fd);
        return;
    }
    ESP_LOGE("WS_DEBUG", "enviando mensaje %s al FD %d", msg, fd);

    // 1. Creamos una copia en el HEAP que no desaparezca al salir de la función
    char* data_copy = strdup(msg);
    if (!data_copy) return;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    // PRUEBA DE FUEGO: Ignoramos 'msg' y enviamos algo fijo
    //static const char* test_msg = "ESP32_ALIVE";
    //ws_pkt.payload = (uint8_t*)test_msg;
    //ws_pkt.len = strlen(test_msg);
    //fin prueba, luego sacar los comentarios de abajo

    ws_pkt.payload = (uint8_t*)data_copy;
    ws_pkt.len = strlen(data_copy);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // 2. Usamos la versión de envío que permite pasar una función de limpieza (opcional pero recomendada)
    // Si tu versión de ESP-IDF no soporta el callback de limpieza, 
    // al menos la copia física ayuda a que no se corrompa el dato inmediatamente.
    esp_err_t ret = httpd_ws_send_frame_async(server, fd, &ws_pkt);

    if (ret != ESP_OK) {
        ESP_LOGE("WS_AS", "Error al encolar: %s", esp_err_to_name(ret));
        free(data_copy);
    } else {
        // NOTA: httpd_ws_send_frame_async en ESP-IDF NO libera el payload automáticamente.
        // Como no queremos fugas de memoria, si el comando se usa miles de veces, 
        // lo ideal sería una respuesta síncrona, pero para probar, 
        // vamos a ver si esto llega al browser primero.
    }
}

/***************************************************
 * Respuesta a TODOS los websockets conectados
 ****************************************************/
// Función que recorre todos los clientes abiertos
void ws_broadcast_message(const char* mensaje) {
    if (!server) return;
    size_t clients = 4; // Ajusta a tu config.max_open_sockets
    int fds[4];

    if (httpd_get_client_list(server, &clients, fds) == ESP_OK) {
        for (size_t i = 0; i < clients; i++) {
            // Verificamos si el socket sigue vivo
            if (httpd_ws_get_fd_info(server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
                continue; 
            }

            httpd_ws_frame_t ws_pkt;
            memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
            ws_pkt.payload = (uint8_t*)mensaje;
            ws_pkt.len = strlen(mensaje);
            ws_pkt.type = HTTPD_WS_TYPE_TEXT;
            ws_pkt.final = true;

            // Intentamos enviar. Si falla aquí o el FD está marcado como erróneo, 
            // el servidor debería cerrarlo, pero vamos a ayudarlo:
            esp_err_t ret = httpd_ws_send_frame_async(server, fds[i], &ws_pkt);
            
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Error enviando a FD %d, cerrando socket...", fds[i]);
                httpd_sess_trigger_close(server, fds[i]); // <--- ESTO es la clave
            }
        }
    }
}
/**********************************
 * Función logout: borra la IP autorizada
 **********************************/    
 
esp_err_t logout_handler(httpd_req_t *req) {
    // Borramos la IP autorizada
    memset(authorized_ip, 0, sizeof(authorized_ip));
    
    ESP_LOGI(TAG, "Sesión cerrada por el usuario.");

    // Enviamos una respuesta y pedimos al navegador que olvide las credenciales 401
    // (Opcional: puedes simplemente redirigir a una página de "Adiós")
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, "Sesion cerrada correctamente.", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
//Cleanup en caso de que se reinicie el wifi

void cleanup_before_wifi_restart() {
    if (server != NULL) {
        size_t clients = 8;
        int fds[8];
        // 1. Obtener todos los descriptores de archivos (sockets) abiertos
        if (httpd_get_client_list(server, &clients, fds) == ESP_OK) {
            for (size_t i = 0; i < clients; i++) {
                // 2. Forzar el cierre de cada socket individualmente
                httpd_sess_trigger_close(server, fds[i]);
                ESP_LOGD("WS_CLEAN", "Cerrando socket FD: %d", fds[i]);
            }
        }
        // Dar un pequeño tiempo para que el servidor procese los cierres
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
// Llama a esto antes de reiniciar el WiFi
void force_session_cleanup() {
    memset(authorized_ip, 0, sizeof(authorized_ip));
    ESP_LOGW(TAG, "Sesiones limpiadas por reinicio de red.");
}

// --- HANDLER: DESCARGA archivos ---
esp_err_t download_handler(httpd_req_t *req)
{
    char query[128];
    char file_param[96];
    char fullpath[160];

    // Obtener file=
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "file", file_param, sizeof(file_param)) == ESP_OK) {

        build_full_path(fullpath, sizeof(fullpath), file_param);

    } else {
        // Sin parámetro → usar MAIN_LOG
        build_full_path(fullpath, sizeof(fullpath), MAIN_LOG);
    }

    ESP_LOGI("WEB", "Abriendo: %s", fullpath);

    FILE *f = fopen(fullpath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Archivo no encontrado");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/octet-stream");

    const char *filename = strrchr(fullpath, '/');
    filename = filename ? filename + 1 : fullpath;

    // Limitar tamaño del nombre
    char safe_name[128];
    strncpy(safe_name, filename, sizeof(safe_name) - 1);
    safe_name[sizeof(safe_name) - 1] = '\0';

    // Buffer grande para evitar truncamiento
    char disp[256];
    snprintf(disp, sizeof(disp),
            "attachment; filename=\"%s\"",
            safe_name);

    httpd_resp_set_hdr(req, "Content-Disposition", disp);


    char *buffer = (char*) malloc(1024);
    size_t n;
    while ((n = fread(buffer, 1, 1024, f)) > 0)
        httpd_resp_send_chunk(req, buffer, n);

    free(buffer);
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}


/* ============================
   HANDLER GET /upload
   Muestra formulario HTML
   ============================ */
static esp_err_t upload_form_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html><body>"
        "<h3>Subir archivo a /fs</h3>"
        "<form method='POST' action='/upload' enctype='multipart/form-data'>"
        "<input type='file' name='file'>"
        "<br><br>"
        "<button type='submit'>Subir</button>"
        "</form>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

/* ============================
   HANDLER POST /upload
   Recibe archivo y lo guarda
   ============================ */
static esp_err_t upload_handler(httpd_req_t *req)
{
    char buf[1024];
    int received;

    // 1) Leer el primer chunk para obtener filename
    received = httpd_req_recv(req, buf, sizeof(buf));
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No se pudo leer multipart");
        return ESP_FAIL;
    }

    buf[received] = '\0';

    // 2) Buscar filename="..."
    char filename[64] = {0};
    char *p = strstr(buf, "filename=\"");
    if (!p) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No se pudo obtener nombre de archivo");
        return ESP_FAIL;
    }

    p += 10; // saltar filename="
    char *q = strchr(p, '"');
    if (!q) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Nombre de archivo inválido");
        return ESP_FAIL;
    }

    int len = q - p;
    if (len > 63) len = 63;
    strncpy(filename, p, len);
    filename[len] = '\0';

    // 3) Construir path final
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    FILE *f = fopen(filepath, "w");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo crear archivo");
        return ESP_FAIL;
    }

    // 4) Saltar headers del multipart hasta encontrar doble CRLF
    char *body = strstr(q, "\r\n\r\n");
    if (!body) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Multipart inválido");
        return ESP_FAIL;
    }

    body += 4; // saltar \r\n\r\n

    int body_len = received - (body - buf);

    // 5) Escribir lo que queda del primer chunk
    fwrite(body, 1, body_len, f);

    // 6) Leer el resto del archivo
    int remaining = req->content_len - received;

    while (remaining > 0) {
        received = httpd_req_recv(req, buf, sizeof(buf));
        if (received <= 0) break;

        fwrite(buf, 1, received, f);
        remaining -= received;
    }

    fclose(f);

    httpd_resp_sendstr(req, "Archivo subido correctamente");
    return ESP_OK;
}



// --- HANDLER: LISTAR ARCHIVOS ---Ahora usa un html externo

static esp_err_t http_ls_handler(httpd_req_t *req) {
    char path[256];
    char query[256];
    char dir_param[128];

    // Por defecto: listar MOUNT_POINT
    snprintf(path, sizeof(path), "%s", MOUNT_POINT);

    // Procesar ?dir=
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "dir", dir_param, sizeof(dir_param)) == ESP_OK) {

            // Normalizar: quitar TODOS los slashes iniciales
            const char *clean = dir_param;
            while (*clean == '/') clean++;

            if (clean[0] != '\0') {
                // Construir ruta final: /fs/clean
                snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, clean);
            }
        }
    }

    ESP_LOGI("WEB", "Listando: %s", path);

    DIR *dr = opendir(path);
    if (!dr) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directorio no encontrado");
        return ESP_FAIL;
    }

    std::string json = "[";

    struct dirent *de;
    struct stat st;
    bool first = true;

    while ((de = readdir(dr)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);

        if (stat(fullpath, &st) == 0) {
            if (!first) json += ",";
            first = false;

            json += "{";
            json += "\"name\":\"" + std::string(de->d_name) + "\",";
            json += "\"size\":" + std::to_string(st.st_size) + ",";
            json += "\"mtime\":" + std::to_string(st.st_mtime);
            json += "}";
        }
    }

    closedir(dr);
    json += "]";

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, json.c_str(), json.size());

    return ESP_OK;
}


static esp_err_t http_more_handler(httpd_req_t *req) {
    char query[256];
    char file_param[128];
    char fullpath[200];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "file", file_param, sizeof(file_param)) == ESP_OK) {

        build_full_path(fullpath, sizeof(fullpath), file_param);

        ESP_LOGI("WEB", "Abriendo: %s", fullpath);

        FILE *f = fopen(fullpath, "r");
        if (!f) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Archivo no encontrado");
            return ESP_FAIL;
        }

        httpd_resp_set_type(req, "text/plain");

        char buffer[512];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
            httpd_resp_send_chunk(req, buffer, n);

        fclose(f);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Uso: /more?file=archivo");
    return ESP_FAIL;
}


static esp_err_t http_rm_handler(httpd_req_t *req) {
    char query[256];
    char file_param[128];
    char fullpath[200];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "file", file_param, sizeof(file_param)) == ESP_OK) {

        build_full_path(fullpath, sizeof(fullpath), file_param);

        ESP_LOGW("WEB", "Intentando borrar: %s", fullpath);

        if (unlink(fullpath) == 0) {
            httpd_resp_set_status(req, "303 See Other");
            httpd_resp_set_hdr(req, "Location", "/ls");
            httpd_resp_send(req, NULL, 0);
            return ESP_OK;
        }

        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo borrar el archivo");
        return ESP_FAIL;
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falta parámetro file");
    return ESP_FAIL;
}


/*
static esp_err_t http_ls_handler(httpd_req_t *req) {
    char query[256];
    // Usamos MOUNT_POINT como base
    std::string base_path = MOUNT_POINT;

    // Procesar query string si existe (?dir=...)
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char dir_param[128];
        if (httpd_query_key_value(query, "dir", dir_param, sizeof(dir_param)) == ESP_OK) {
            if (strstr(dir_param, MOUNT_POINT) == NULL) {
                base_path = std::string(MOUNT_POINT) + (dir_param[0] == '/' ? "" : "/") + dir_param;
            } else {
                base_path = dir_param;
            }
        }
    }

    DIR *dr = opendir(base_path.c_str());
    if (dr == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No se pudo abrir el directorio");
        return ESP_FAIL;
    }

    // Construcción del HTML con soporte UTF-8 completo
    std::string response = "<html><head>"
                           "<meta charset='UTF-8'>"
                           "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                           "<style>"
                           "body { font-family: sans-serif; margin: 20px; background-color: #f4f4f9; color: #333; }"
                           "table { width: 100%; border-collapse: collapse; background: white; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }"
                           "th, td { text-align: left; padding: 12px; border-bottom: 1px solid #ddd; }"
                           "th { background-color: #007bff; color: white; }"
                           "tr:hover { background-color: #f1f1f1; }"
                           ".btn { text-decoration: none; padding: 5px 10px; border-radius: 4px; font-size: 14px; }"
                           ".btn-view { color: #007bff; }"
                           ".btn-delete { color: #dc3545; font-weight: bold; }"
                           "footer { margin-top: 20px; font-size: 0.8em; color: #666; }"
                           "</style></head><body>";
    
    response += "<h2>📂 Explorador de Archivos</h2>";
    response += "<p>Directorio actual: <strong>" + base_path + "</strong></p>";
    
    response += "<table><tr>"
                "<th>Nombre</th>"
                "<th>Tamaño</th>"
                "<th>Fecha y Hora</th>"
                "<th>Acciones</th>"
                "</tr>";

    struct dirent *de;
    struct stat st;

    while ((de = readdir(dr)) != NULL) {
        std::string filename = de->d_name;
        if (filename == "." || filename == "..") continue;

        std::string full_path = base_path + "/" + filename;
        response += "<tr>";
        
        if (stat(full_path.c_str(), &st) == 0) {
            // 1. Nombre (RESTAURADO EL HIPERVÍNCULO AQUÍ)
            response += "<td><a href='/more?file=/" + filename + "'><strong>" + filename + "</strong></a></td>";
            
            // 2. Tamaño
            response += "<td>";
            if (st.st_size >= 1024) response += std::to_string(st.st_size / 1024) + " KB";
            else response += std::to_string(st.st_size) + " B";
            response += "</td>";

            // 3. Fecha y Hora
            char date_buf[25];
            struct tm *tm_info = localtime(&st.st_mtime);
            strftime(date_buf, sizeof(date_buf), "%d/%m/%Y %H:%M:%S", tm_info);
            response += "<td>" + std::string(date_buf) + "</td>";

            // 4. Acciones
            response += "<td>"
                        "<a class='btn btn-view' href='/more?file=/" + filename + "' title='Ver'>👁️</a> "
                        "<a class='btn btn-delete' href='/rm?file=/" + filename + "' "
                        "onclick=\"return confirm('¿Estás seguro de borrar " + filename + "?')\" title='Borrar'>🗑️</a>"
                        "</td>";
        } else {
            response += "<td>" + filename + "</td><td colspan='3'>Error al leer metadatos</td>";
        }
        response += "</tr>";
    }
    
    closedir(dr);
    response += "</table>";
    response += "<footer>Punto de montaje: " + std::string(MOUNT_POINT) + "</footer>";
    response += "</body></html>";

    // Forzamos el tipo de contenido y el charset en el header HTTP
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response.c_str(), HTTPD_RESP_USE_STRLEN);
    
    return ESP_OK;
}

*/
/*
// --- HANDLER: LEER ARCHIVO COMPLETO ---
static esp_err_t http_more_handler(httpd_req_t *req) {
    char query[256];
    char file_param[128]; // Mantenlo en un tamaño razonable

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "file", file_param, sizeof(file_param)) == ESP_OK) { //more?file=/log.txt
            
            // USAMOS std::string: El compilador no se quejará de tamaños fijos
            std::string path_str = MOUNT_POINT;
            
            // Si el parámetro no empieza con '/', lo agregamos
            if (file_param[0] != '/') {
                path_str += "/";
            }
            path_str += file_param;

            ESP_LOGI("WEB", "Abriendo: %s", path_str.c_str());

            // Usamos path_str.c_str() para abrir el archivo
            FILE* f = fopen(path_str.c_str(), "r");
            if (f == NULL) {
                char msg[160];
                snprintf(msg, sizeof(msg), "Archivo no encontrado: %s", path_str.c_str());

                ESP_LOGE("WEB", "%s", msg);
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, msg);
                return ESP_FAIL;
            }

            httpd_resp_set_type(req, "text/plain");
            char buffer[512];
            size_t read_bytes;
            while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                httpd_resp_send_chunk(req, buffer, read_bytes);
            }
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0); 
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Uso: /more?file=/log.txt");
    return ESP_FAIL;
} 

//borrar archivo
static esp_err_t http_rm_handler(httpd_req_t *req) {
    char query[256];
    char file_param[128];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "file", file_param, sizeof(file_param)) == ESP_OK) {
            
            // Construimos la ruta completa con MOUNT_POINT
            std::string path_to_del = MOUNT_POINT;
            if (file_param[0] != '/') path_to_del += "/";
            path_to_del += file_param;

            ESP_LOGW("WEB", "Intentando borrar: %s", path_to_del.c_str());

            // Intentar borrar el archivo
            if (unlink(path_to_del.c_str()) == 0) {
                // Si borra con éxito, redirigimos automáticamente al listado (ls)
                httpd_resp_set_status(req, "303 See Other");
                httpd_resp_set_hdr(req, "Location", "/ls");
                httpd_resp_send(req, NULL, 0);
                return ESP_OK;
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo borrar el archivo");
                return ESP_FAIL;
            }
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falta parámetro file");
    return ESP_FAIL;
}
*/


