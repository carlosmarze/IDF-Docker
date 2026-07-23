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

#include "utils_cmd_dispatcher.h"
#include "utils_cmd_processor.h"
#include "utils_ota_worker.h"
#include "utils_cmd_parser.h"
#include "utils_webs.h"
#include "utils_time.h"
#include "utils_config.h"
#include "utils_logger.h"
#include "utils_mqtt.h"           // AÑADIDO: para publicar respuesta
#include "utils_bt.h"           // AÑADIDO: para publicar respuesta
#include "MisVariablesProyecto.h" // AÑADIDO: para SensorID

static const char *TAG = "CMD";

// -----------------------------------------------------------------------------
// Utilidades
// -----------------------------------------------------------------------------

static inline void trim_inplace(std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        s.clear();
        return;
    }
    size_t end = s.find_last_not_of(" \t\n\r");
    s = s.substr(start, end - start + 1);
}

static std::vector<std::string> tokenize(const std::string &s) {
    std::vector<std::string> args;
    std::stringstream ss(s);
    std::string tmp;
    while (ss >> tmp) {
        args.push_back(tmp);
    }
    return args;
}

// -----------------------------------------------------------------------------
// CommandDispatcher
// -----------------------------------------------------------------------------

CommandDispatcher::CommandDispatcher() : s_q(NULL), web_server(NULL) {
    s_q = xQueueCreate(10, sizeof(cmd_msg_t));
}

void CommandDispatcher::setWebServer(httpd_handle_t server) {
    this->web_server = server;
}

void CommandDispatcher::registerCommand(std::unique_ptr<Command> cmd) {
    commands[cmd->name()] = std::move(cmd);
}

bool CommandDispatcher::submit(const cmd_msg_t &msg) {
    //vamos a agregar aquí el flag para loguear o no cada comando, por ejemplo para comandos internos o de debug que no queremos saturar el log, pero que igual queremos procesar

    if (!s_q) return false;
    return xQueueSendToBack(s_q, &msg, 0) == pdTRUE;
}

bool CommandDispatcher::submit(cmd_source_t src, const char *text) {
    process_commands(src, text, ' ', '=', -1);
    return true;
}

void CommandDispatcher::start() {
    xTaskCreate(taskEntry, "disp_task", 8192, this, 5, NULL);
}

void CommandDispatcher::taskEntry(void *arg) {
    static_cast<CommandDispatcher*>(arg)->dispatcherTask();
}


//Versión de QWEN studio, con variables static declaradas una sola vez al principio para minimizar el uso de stack y evitar problemas de fragmentación, además de limpiar el vector de argumentos en cada iteración para evitar que se acumulen los argumentos de comandos anteriores en comandos nuevos, lo cual causaba errores difíciles de depurar. También agregué más logs para tener un registro claro de lo que se ejecuta y responde, y para detectar posibles problemas en el envío por WebSocket o MQTT. Además, agregué comentarios explicativos en cada sección del código para facilitar su comprensión y mantenimiento futuro.
void CommandDispatcher::dispatcherTask() {

    // 1. DECLARAR UNA SOLA VEZ AL PRINCIPIO (Todas en el segmento BSS, 0 bytes de stack)
    static cmd_msg_t m;
    //static std::string log_msg;
    static std::string cmname;
    static std::string buf;
    static std::vector<std::string> args;
    static std::string respuesta;
    static char res_topic[MAX_TOPIC_LENGTH]; // Movido aquí para ahorrar stack
    static httpd_ws_frame_t ws_pkt;          // Movido aquí para ahorrar stack

    while (true) {

        if (xQueueReceive(s_q, &m, portMAX_DELAY) != pdTRUE)
            continue;

        // 2. ASIGNAR VALORES (Sin escribir el tipo de dato ni static)
        cmname = m.name;
        buf    = m.arg;
        
        // ⚠️ CRÍTICO: Como args es static, hay que limpiarlo en cada iteración
        // Si no lo haces, los argumentos del comando anterior se sumarán a los nuevos
        args.clear(); 

        auto it = commands.find(cmname);
        if (it == commands.end()) {
            respuesta = "Error: Comando desconocido '" + cmname + "'";
            ESP_LOGW(TAG, "%s", respuesta.c_str());
            continue;
        }

        Command *cmd = it->second.get();

        // ============================================================
        // 🔥 LÓGICA UNIVERSAL DE ARGUMENTOS (NUEVA)
        // ============================================================

        if (!buf.empty()) {
            auto ptokens = parse_lineX(cmname + " " + buf);

            if (ptokens.size() <= 1) {
                if (cmd->minArgs() <= 1) {
                    args.push_back(buf);
                }
            } 
            else {
                if (cmd->positionalArgs()) {
                    // POSICIONAL → usar name (no arg)
                    for (size_t i = 1; i < ptokens.size(); i++) {
                        if (!ptokens[i].name.empty()) {
                            args.push_back(ptokens[i].name);  // ← CAMBIAR AQUÍ
                        }
                    }
                } 
                else {
                    // NORMAL → usar key=value si existe
                    for (size_t i = 1; i < ptokens.size(); i++) {
                        if (ptokens[i].arg.empty()) {
                            args.push_back(ptokens[i].name);
                        } else {
                            args.push_back(ptokens[i].name + "=" + ptokens[i].arg);
                        }
                    }
                }
            }
        }  

        if(m.src == CMD_SRC_SYSTEM) { //comandos del scheduler por ej
            ESP_LOGI(TAG, "SRC: %d - Comando: %s %s args#:%d", m.src, cmname.c_str(), m.arg, (int)args.size()); //ver de mejorar
        }
        else {
            LOGI(TAG, "SRC: %d - Comando: %s %s args#:%d", m.src, cmname.c_str(), m.arg, (int)args.size());
        }

        //write_system_log(TAG, log_msg.c_str());
        //ESP_LOGI(TAG, "%s", log_msg.c_str());

        // 3. YA NO HACE FALTA DECLARAR "static std::string respuesta;" AQUÍ
        if ((int)args.size() < cmd->minArgs()) {
            respuesta = "Error: Faltan argumentos para '" + cmname + "'";
            ESP_LOGW(TAG, "%s", respuesta.c_str());
        } else {
            ESP_LOGI(TAG, "Stack libre pre-ejecución: %d bytes", uxTaskGetStackHighWaterMark(NULL) * 4);
            respuesta = cmd->execute(m.src, args);
            ESP_LOGI(TAG, "Stack libre post-ejecución: %d bytes", uxTaskGetStackHighWaterMark(NULL) * 4);
        }

        // ============================================================
        // 🔥 RUTEO DE RESPUESTA
        // ============================================================
        if (m.log) { 
            LOGN(TAG, "%s", respuesta.c_str()); //Solo graba en el archivo, no en el log de consola, para evitar saturar el log con respuestas largas o frecuentes, pero dejando un registro en el archivo de lo que se ejecutó y respondió.
        }
        ESP_LOGI(TAG, "Respuesta: %s", respuesta.c_str());

        if (m.src == CMD_SRC_MQTT) {

            // Usamos la variable static que declaramos al principio
            generar_topico_mqtt("A", SensorID, res_topic, sizeof(res_topic));
            publish_mqtt(res_topic, respuesta.c_str(), 0, 0);

            ESP_LOGI(TAG, "Respuesta MQTT publicada en %s", res_topic);
        }
        else if (m.src == CMD_SRC_WEB) {

            if (this->web_server != nullptr && m.client_fd != -1) {

                // Usamos la variable static que declaramos al principio
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t*)respuesta.c_str();
                ws_pkt.len = respuesta.length();
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                esp_err_t ret = httpd_ws_send_data(this->web_server, m.client_fd, &ws_pkt);

                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Respuesta enviada por WebSocket a FD: %d", m.client_fd);
                } else {
                    ESP_LOGE(TAG, "Error enviando por WebSocket: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGW(TAG, "No se pudo enviar respuesta Web: Server nulo o FD -1");
                ESP_LOGI(TAG, "Respuesta (sin destino): %s", respuesta.c_str());
            }
        }
        else if (m.src == CMD_SRC_BT) {

            ble_uart_send(respuesta.c_str());
            ESP_LOGI(TAG, "Respuesta enviada por BLE UART");

        }
        else {
            printf("\r\n> %s\r\n", respuesta.c_str()); 
        }
    }
}


CommandDispatcher dispatcher; //objeto real
CommandDispatcher* global_dispatcher_ptr = nullptr; //Lo asignaremos en app_main antes de usarlopor primera vez.  Definición del objeto global del despachador, que se puede usar en todo el proyecto, por ejemplo en el main para registrar comandos, o en los handlers web para enviar comandos al despachador, etc.