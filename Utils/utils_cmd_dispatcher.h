#ifndef UTILS_CMD_DISPATCHER_H
#define UTILS_CMD_DISPATCHER_H

#include <stddef.h>
#include <stdbool.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fuente del comando (UART, MQTT, Web, etc.)
typedef enum {
    CMD_SRC_UART = 0,
    CMD_SRC_MQTT,
    CMD_SRC_WEB,
    CMD_SRC_BT,
    CMD_SRC_FILE,
    CMD_SRC_SYSTEM
} cmd_source_t;

// Estructura para la cola de comandos
typedef struct {
    cmd_source_t src;   // origen del comando
    char name[32];      // nombre del comando
    char arg[512];      // argumentos
    int client_fd;      // descriptor para respuestas web (WebSocket/HTTP)
    bool log = true;       // si es true, se loguea el comando recibido y la respuesta, si es false, no se loguea (útil para comandos internos o de debug que no queremos saturar el log)
} cmd_msg_t;

#ifdef __cplusplus
}

// Clase base para todos los comandos
class Command {
public:
    virtual ~Command() {}

    // Retorna el nombre del comando (ej: "SSID")
    virtual const char* name() const = 0;

    // Retorna la ayuda o modo de uso (necesario para Help y evitar error de override)
    virtual const char* usage() const = 0; 

    // Ejecuta la acción y devuelve la respuesta como string
    // El const en el vector es importante para que coincida con tus clases en .cpp
    virtual std::string execute(cmd_source_t src, const std::vector<std::string>& args) = 0;
    
    // 🔥 NUEVO: por defecto, los comandos NO son posicionales
    virtual bool positionalArgs() const { return false; }

    // Retorna el número mínimo de argumentos requeridos
    virtual int minArgs() const { return 0; }
};

class CommandDispatcher {
public:
    CommandDispatcher();
    
    // Configura el servidor web para respuestas asíncronas
    void setWebServer(httpd_handle_t server);

    // Inicia la tarea del despachador
    void start();

    // Envía un texto para procesar (lo desglosa en comandos)
    bool submit(cmd_source_t src, const char *text);

    // Envía un mensaje estructurado directamente a la cola
    bool submit(const cmd_msg_t &msg);
    
    // Registra un nuevo comando en el sistema
    void registerCommand(std::unique_ptr<Command> cmd);

    // Bucle principal de la tarea (público para ser llamado por la tarea FreeRTOS)
    void dispatcherTask(); 

    // Retorna el mapa de comandos (usado por el comando Help)
    const std::map<std::string, std::unique_ptr<Command>>& getCommands() const {
        return commands;
    }
    Command* getCommand(const std::string& name) const { //no confundir con el anterior, que trae todos los comandos, este trae solo el comando que se le pide por nombre
        auto it = commands.find(name);
        if (it == commands.end()) return nullptr;
        return it->second.get();
    }

private:
    std::map<std::string, std::unique_ptr<Command>> commands;
    QueueHandle_t s_q;
    static void taskEntry(void *arg);
    httpd_handle_t web_server; 
};
extern CommandDispatcher dispatcher;
// Objeto global para ser usado en todo el proyecto
extern CommandDispatcher* global_dispatcher_ptr; 

#endif

#endif // UTILS_CMD_DISPATCHER_H