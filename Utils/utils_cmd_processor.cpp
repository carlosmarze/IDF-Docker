#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string>
#include <map>
#include <memory>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <cJSON.h>

#include "utils_cmd_processor.h"
#include "utils_cmd_dispatcher.h"
#include "utils_cmd_parser.h"

static const char *TAG = "PROC";

static inline std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

//propuesta QWEN
// Función auxiliar para escapar argumentos con espacios
static std::string escape_arg(const std::string& arg) {
    // Si contiene espacios, envolver en comillas
    if (arg.find(' ') != std::string::npos || arg.find('\t') != std::string::npos) {
        return "\"" + arg + "\"";
    }
    return arg;
}


// Función auxiliar para convertir cJSON a string
static std::string json_to_string(cJSON* json) {
    if (!json) return "";
    char* str = cJSON_PrintUnformatted(json);
    std::string result = str ? str : "";
    free(str);
    return result;
}

// Procesa comandos en formato JSON
static bool process_json_command(cmd_source_t src, const std::string& json_str, int client_fd) {
    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Error parseando JSON");
        return false;
    }

    // Extraer comando
    cJSON* cmd_json = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd_json) || cmd_json->valuestring == nullptr) {
        ESP_LOGE(TAG, "JSON sin campo 'cmd'");
        cJSON_Delete(root);
        return false;
    }

    std::string cmd_name = cmd_json->valuestring;
    std::vector<std::string> args;

    // Caso 1: {"cmd":"setssid","arg":"DepartamentoJ"}
    cJSON* arg_json = cJSON_GetObjectItem(root, "arg");
    if (arg_json) {
        if (cJSON_IsString(arg_json)) {
            // Argumento simple como string
            args.push_back(arg_json->valuestring);
        } else if (cJSON_IsObject(arg_json)) {
            // Argumento como objeto: {"server":"x","chunksize":1024}
            // Convertir a formato key=value
            std::string arg_str;
            cJSON* child = arg_json->child;
            while (child) {
                if (!arg_str.empty()) arg_str += " ";
                arg_str += child->string;
                arg_str += "=";
                if (cJSON_IsString(child)) {
                    arg_str += child->valuestring;
                } else if (cJSON_IsNumber(child)) {
                    arg_str += std::to_string(child->valueint);
                } else {
                    arg_str += json_to_string(child);
                }
                child = child->next;
            }
            args.push_back(arg_str);
        }
    }

    // Caso 2: {"cmd":"setwifi","args":["Departamento J","el departamento"]}
    cJSON* args_json = cJSON_GetObjectItem(root, "args");
    if (args_json && cJSON_IsArray(args_json)) {
        cJSON* item = args_json->child;
        while (item) {
            if (cJSON_IsString(item)) {
                args.push_back(item->valuestring);
            } else {
                args.push_back(json_to_string(item));
            }
            item = item->next;
        }
    }

    cJSON_Delete(root);

    // Crear mensaje y enviar al dispatcher
    cmd_msg_t msg{};
    msg.src = src;
    msg.client_fd = client_fd;
    snprintf(msg.name, sizeof(msg.name), "%s", cmd_name.c_str());
    
    // Concatenar todos los argumentos
    std::string full_args;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) full_args += " ";
        full_args += escape_arg(args[i]);  // ← ESCAPAR AQUÍ
    }
    snprintf(msg.arg, sizeof(msg.arg), "%s", full_args.c_str());

    // Desactivar log para comandos largos
    if (strcmp(msg.name, "help") == 0 || strcmp(msg.name, "dir") == 0 ||
        strcmp(msg.name, "webupdate") == 0 || strcmp(msg.name, "more") == 0) {
        msg.log = false;
    }

    dispatcher.submit(msg);
    return true;
}

// Función principal de procesamiento de comandos
void process_commands(cmd_source_t src, const std::string &line, char sep1, char sep2, int client_fd) {
    ESP_LOGI(TAG, "Stack libre en proc: %u bytes", uxTaskGetStackHighWaterMark(NULL));
    printf("Linea cruda: %s", line.c_str());
    // Trim inicial
    std::string trimmed = line;
    while (!trimmed.empty() && std::isspace(trimmed.front())) trimmed.erase(0, 1);
    while (!trimmed.empty() && std::isspace(trimmed.back())) trimmed.pop_back();
    
    if (trimmed.empty()) {
        ESP_LOGW(TAG, "Línea vacía");
        return;
    }

    // Detectar si es JSON
    bool is_json = (trimmed.front() == '{' && trimmed.back() == '}');
    
    if (is_json) {
        ESP_LOGI(TAG, "Procesando comando JSON");
        if (process_json_command(src, trimmed, client_fd)) {
            return;
        }
        ESP_LOGW(TAG, "Error procesando JSON, intentando como comando normal");
    }

    // Procesar como comando normal (no JSON)
    auto tokens = parse_lineX(trimmed);
    
    if (tokens.empty()) {
        ESP_LOGW(TAG, "No se pudieron parsear argumentos");
        return;
    }

    // Caso 1: Un solo token (comando sin argumentos)
    if (tokens.size() == 1) {
        cmd_msg_t msg{};
        msg.src = src;
        msg.client_fd = client_fd;
        snprintf(msg.name, sizeof(msg.name), "%s", tokens[0].name.c_str());
        snprintf(msg.arg, sizeof(msg.arg), "%s", tokens[0].arg.c_str());
        
        if (strcmp(msg.name, "help") == 0 || strcmp(msg.name, "dir") == 0 ||
            strcmp(msg.name, "webupdate") == 0 || strcmp(msg.name, "more") == 0) {
            msg.log = false;
        }
        printf("Procesado1: name=%s arg=%s", msg.name, msg.arg);
        dispatcher.submit(msg);
        return;
    }

    // Caso 2: Primer token sin argumento, resto son argumentos posicionales
    // Ejemplo: setwifi "Departamento J" "el departamento"
    if (tokens[0].arg.empty() && tokens[1].arg.empty()) {
        std::string comando = tokens[0].name;
        std::string args_concat;
        
        for (size_t i = 1; i < tokens.size(); i++) {
            if (!args_concat.empty()) args_concat += " ";
             args_concat += escape_arg(tokens[i].name);  // ← ESCAPAR AQUÍ
        }
        
        cmd_msg_t msg{};
        msg.src = src;
        msg.client_fd = client_fd;
        snprintf(msg.name, sizeof(msg.name), "%s", comando.c_str());
        snprintf(msg.arg, sizeof(msg.arg), "%s", args_concat.c_str());
        
        if (strcmp(msg.name, "help") == 0 || strcmp(msg.name, "dir") == 0 ||
            strcmp(msg.name, "webupdate") == 0 || strcmp(msg.name, "more") == 0) {
            msg.log = false;
        }
        printf("Procesado2: name=%s arg=%s", msg.name, msg.arg);
        dispatcher.submit(msg);
        return;
    }

    // Caso 3: Múltiples comandos con sus argumentos (formato key=value)
    // Ejemplo: ota server=http://x chunksize=2048 reboot=no
    for (auto &pt : tokens) {
        cmd_msg_t msg{};
        msg.src = src;
        msg.client_fd = client_fd;
        snprintf(msg.name, sizeof(msg.name), "%s", pt.name.c_str());
        snprintf(msg.arg, sizeof(msg.arg), "%s", pt.arg.c_str());
        
        if (strcmp(msg.name, "help") == 0 || strcmp(msg.name, "dir") == 0 ||
            strcmp(msg.name, "webupdate") == 0 || strcmp(msg.name, "more") == 0) {
            msg.log = false;
        }
        printf("Procesado3: name=%s arg=%s", msg.name, msg.arg);
        dispatcher.submit(msg);
    }
}


