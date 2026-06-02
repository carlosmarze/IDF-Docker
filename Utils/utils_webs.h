#ifndef UTILS_WEBS_H
#define UTILS_WEBS_H

#include "esp_err.h"
#include "esp_http_server.h"

// Define el punto de montaje aquí para compartirlo si es necesario
//#define MOUNT_POINT "/www"
//#define MOUNT_POINT "/fs"

// --- Declaración anticipada ---
// Esto le dice al compilador: "Habrá una clase llamada CommandDispatcher, 
// no te preocupes por los detalles ahora, solo necesito saber que existe".
class CommandDispatcher;
// Función para iniciar el servidor web
void start_webserver(CommandDispatcher* disp_ptr);
void enviar_respuesta_async(httpd_handle_t server, int fd, const char* msg);
// Función para enviar desde cualquier comando a TODOS los websockets conectados
void ws_broadcast_message(const char* mensaje);

// Función para inicializar (guarda el handle del server)
void set_ws_server_handle(httpd_handle_t handle);
void cleanup_before_wifi_restart();
void force_session_cleanup(); // Llama a esto antes de reiniciar el WiFi
extern httpd_handle_t global_webserver_handle;
bool server_running();
void stop_webserver();


#endif // WEB_SERVER_H