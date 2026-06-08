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
    xTaskCreate(taskEntry, "disp_task", 4096, this, 5, NULL);
}

void CommandDispatcher::taskEntry(void *arg) {
    static_cast<CommandDispatcher*>(arg)->dispatcherTask();
}

/*
void CommandDispatcher::dispatcherTask() { //Acá me llega un comando en m.name y su argumento en m.arg, lo busco en el mapa de comandos, lo ejecuto y envío la respuesta al origen correspondiente (MQTT, Web o UART)
    cmd_msg_t m;
    while (true) {
        if (xQueueReceive(s_q, &m, portMAX_DELAY) == pdTRUE) {
            std::string cmname(m.name);
            std::string buf(m.arg);

            trim_inplace(cmname);
            trim_inplace(buf);

            ESP_LOGI(TAG, "Procesando '%s' con arg '%s'", cmname.c_str(), buf.c_str());

            std::string respuesta;
            auto it = commands.find(cmname);
            
            if (it == commands.end()) {
                respuesta = "Error: Comando desconocido '" + cmname + "'";
                ESP_LOGW(TAG, "%s", respuesta.c_str());
            } else {
                std::vector<std::string> args;
                if (!buf.empty()) {
                    args = tokenize(buf);
                }

                Command *cmd = it->second.get();

                if ((int)args.size() < cmd->minArgs()) {
                    respuesta = "Error: Faltan argumentos para '" + cmname + "'";
                    ESP_LOGW(TAG, "%s", respuesta.c_str());
                } else {
                    // Ejecución real: Capturamos el string de respuesta
                    respuesta = cmd->execute(m.src, args); //ya tengo el comando, le paso el origen y el vector de argumentos, y lo ejecut y me devuelve la respuesta 
                }
            }

            // --- Lógica de enrutamiento de respuesta ---
            if (m.src == CMD_SRC_MQTT) {
                char res_topic[MAX_TOPIC_LENGTH];
                generar_topico_mqtt("A", SensorID, res_topic, sizeof(res_topic));
                publish_mqtt(res_topic, respuesta.c_str(), 0, 0);
                ESP_LOGI(TAG, "Respuesta MQTT publicada en %s", res_topic);
            } 
            else if (m.src == CMD_SRC_WEB) {
                // Aquí podrías enviar la respuesta por WebSocket si m.client_fd es válido
                // Verificamos que tengamos un descriptor de socket (fd) y el servidor configurado
                if (this->web_server != nullptr && m.client_fd != -1) {
                    
                    httpd_ws_frame_t ws_pkt;
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
                    // Opcionalmente podrías imprimirlo en el log para debug
                    ESP_LOGI(TAG, "Respuesta (sin destino): %s", respuesta.c_str());
                }
            } 
            else {
                // UART u otros orígenes locales
                printf("\r\n> %s\r\n", respuesta.c_str());
            }
        }
    }
}
*/


/*
void CommandDispatcher::dispatcherTask() {

    static cmd_msg_t m;
    static std::string log_msg;
    static std::string cmname;
    static std::string buf;
    static std::vector<std::string> args;
    static std::string respuesta;

    while (true) {

        if (xQueueReceive(s_q, &m, portMAX_DELAY) != pdTRUE)
            continue;

        static std::string cmname = std::string(m.name);
        std::string buf    = std::string(m.arg);

        //ESP_LOGI(TAG, "Recibido crudo: '%s %s'", cmname.c_str(), buf.c_str());

        auto it = commands.find(cmname);
        if (it == commands.end()) {
            std::string respuesta = "Error: Comando desconocido '" + cmname + "'";
            ESP_LOGW(TAG, "%s", respuesta.c_str());
            continue;
        }

        Command *cmd = it->second.get();
        std::vector<std::string> args;

        // ============================================================
        // 🔥 LÓGICA UNIVERSAL DE ARGUMENTOS
        // ============================================================

        if (!buf.empty()) {

            if (cmd->positionalArgs()) {
                // Ej: more /wifi.json -100
                args = tokenize(buf);
            }
            else if (cmd->minArgs() <= 1) {
                // Ej: setwifipass "mi password con espacios"
                args.push_back(buf);
            }
            else {
                // Ej: ota server=... https=yes chunk=1024
                args = tokenize(buf);
            }
        }

        log_msg = "SRC: " + std::to_string(m.src) + " - Comando: " + cmname + " " + std::string(m.arg) + " args#:" + std::to_string(args.size()) ;
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG, "%s", log_msg.c_str());

        //ESP_LOGI(TAG, "Comando='%s' Args=%d", cmname.c_str(), (int)args.size());

        static std::string respuesta;

        if ((int)args.size() < cmd->minArgs()) {
            respuesta = "Error: Faltan argumentos para '" + cmname + "'";
            ESP_LOGW(TAG, "%s", respuesta.c_str());
        } else {
            //Aquí realmente ejecuta el comando, lo logueamos antes de ejecutarlo para tener registro de lo que se intentó ejecutar, incluso si el comando falla o tarda mucho en responder, y capturamos la respuesta para loguearla también
            
            //log_msg = "Ejecutando comando '" + cmname + "'...";
            //write_system_log(TAG, log_msg.c_str());
            //ESP_LOGI(TAG, "%s", log_msg.c_str());
            // Ejecución real: Capturamos el string de respuesta
            ESP_LOGI(TAG, "Stack libre pre-ejecución: %d bytes", uxTaskGetStackHighWaterMark(NULL) * 4);
            respuesta = cmd->execute(m.src, args);
            ESP_LOGI(TAG, "Stack libre post-ejecución: %d bytes", uxTaskGetStackHighWaterMark(NULL) * 4);
        }

        // ============================================================
        // 🔥 RUTEO DE RESPUESTA: El comando ya fue ejecutado
        // ============================================================
        //loggeo la respuesta antes de enviarla, para tener un registro de lo que se respondió a cada comando, incluso si el envío falla (ej: WebSocket cerrado)
        if (m.log) { //Por default, todos los comandos se loguean, pero si process_command le puso m.log = false, no se loguea la respuesta, para comandos internos o de debug que no queremos saturar el log pero igual queremos procesar
            write_system_log(TAG, respuesta.c_str());
        }
        ESP_LOGI(TAG, "Respuesta: %s", respuesta.c_str());

        if (m.src == CMD_SRC_MQTT) {

            char res_topic[MAX_TOPIC_LENGTH];
            generar_topico_mqtt("A", SensorID, res_topic, sizeof(res_topic));
            publish_mqtt(res_topic, respuesta.c_str(), 0, 0);

            ESP_LOGI(TAG, "Respuesta MQTT publicada en %s", res_topic);
        }
        else if (m.src == CMD_SRC_WEB) {

            if (this->web_server != nullptr && m.client_fd != -1) {

                httpd_ws_frame_t ws_pkt;
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
        else {
            
            printf("\r\n> %s\r\n", respuesta.c_str()); //para UART o comandos internos sin destino específico, lo dejo en consola y log pero no lo envío por WebSocket ni MQTT, para no saturar esos canales con respuestas que no las necesitan
            //write_system_log(TAG, respuesta.c_str()); //Comando desde UART o interno, lo dejo solo en el log para no saturar la consola, que es más para debug puntual
            //ESP_LOGI(TAG, "%s", respuesta.c_str());
        }
    }
}
*/
//Versión de QWEN studio, con variables static declaradas una sola vez al principio para minimizar el uso de stack y evitar problemas de fragmentación, además de limpiar el vector de argumentos en cada iteración para evitar que se acumulen los argumentos de comandos anteriores en comandos nuevos, lo cual causaba errores difíciles de depurar. También agregué más logs para tener un registro claro de lo que se ejecuta y responde, y para detectar posibles problemas en el envío por WebSocket o MQTT. Además, agregué comentarios explicativos en cada sección del código para facilitar su comprensión y mantenimiento futuro.
void CommandDispatcher::dispatcherTask() {

    // 1. DECLARAR UNA SOLA VEZ AL PRINCIPIO (Todas en el segmento BSS, 0 bytes de stack)
    static cmd_msg_t m;
    static std::string log_msg;
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
            // Asignar a la variable static, no crear una nueva
            respuesta = "Error: Comando desconocido '" + cmname + "'";
            ESP_LOGW(TAG, "%s", respuesta.c_str());
            continue;
        }

        Command *cmd = it->second.get();

        // ============================================================
        // 🔥 LÓGICA UNIVERSAL DE ARGUMENTOS
        // ============================================================

        if (!buf.empty()) {

            if (cmd->positionalArgs()) {
                args = tokenize(buf);
            }
            else if (cmd->minArgs() <= 1) {
                args.push_back(buf);
            }
            else {
                args = tokenize(buf);
            }
        }

        log_msg = "SRC: " + std::to_string(m.src) + " - Comando: " + cmname + " " + std::string(m.arg) + " args#:" + std::to_string(args.size()) ;
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG, "%s", log_msg.c_str());

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
            write_system_log(TAG, respuesta.c_str());
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
        else {
            printf("\r\n> %s\r\n", respuesta.c_str()); 
        }
    }
}


CommandDispatcher dispatcher; //objeto real
CommandDispatcher* global_dispatcher_ptr = nullptr; //Lo asignaremos en app_main antes de usarlopor primera vez.  Definición del objeto global del despachador, que se puede usar en todo el proyecto, por ejemplo en el main para registrar comandos, o en los handlers web para enviar comandos al despachador, etc.