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

void process_commands(cmd_source_t src, const std::string &line, char sep1, char sep2, int client_fd) {
      
    //printf("Process_commands: Línea cruda: '%s'\n", line.c_str());
    ESP_LOGI(TAG, "Stack libre en proc: %u bytes", uxTaskGetStackHighWaterMark(NULL));
    auto tokens = parse_lineX(line);

    if (tokens.empty()) {
        printf("Process_commands: línea vacía o inválida\n");
        return;
    }

    std::string trimmed = trim(line);
    bool is_json = !trimmed.empty() && trimmed.front() == '{' && trimmed.back() == '}';

    if (is_json) {
        std::string comando = tokens[0].name;
        std::string args_concat;

        if (!tokens[0].arg.empty()) {
            args_concat = tokens[0].arg;
        }

        for (size_t i = 1; i < tokens.size(); i++) {
            if (!args_concat.empty()) args_concat += " ";

            if (tokens[i].arg.empty()) {
                args_concat += tokens[i].name;
            } else {
                args_concat += tokens[i].name + "=" + tokens[i].arg;
            }
        }

        //printf("Process_commands: comando='%s' args='%s'\n", comando.c_str(), args_concat.c_str());

        cmd_msg_t msg{};
        msg.src = src;
        msg.client_fd = client_fd;
        snprintf(msg.name, sizeof(msg.name), "%s", comando.c_str());
        snprintf(msg.arg, sizeof(msg.arg), "%s", args_concat.c_str());

        if (strcmp(msg.name, "help") == 0 ||
            strcmp(msg.name, "dir") == 0 ||
            strcmp(msg.name, "webupdate") == 0 ||
            strcmp(msg.name, "more") == 0) 
        {
            msg.log = false; //por default es true, si es falso solo logueamos el comando pero no la respuesta, para no saturar el log con respuestas largas de comandos como help o dir o more, pero igual queremos que se procesen y se puedan usar
        }
        
        dispatcher.submit(msg);
        return;
    }

    if (tokens.size() == 1) {
        cmd_msg_t msg{};
        msg.src = src;
        msg.client_fd = client_fd;
        snprintf(msg.name, sizeof(msg.name), "%s", tokens[0].name.c_str());
        snprintf(msg.arg, sizeof(msg.arg), "%s", tokens[0].arg.c_str());

        //printf("Process_commands: comando='%s' args='%s'\n", msg.name, msg.arg);
        if (strcmp(msg.name, "help") == 0 ||
            strcmp(msg.name, "dir") == 0 ||
            strcmp(msg.name, "webupdate") == 0 ||
            strcmp(msg.name, "more") == 0) 
        {
            msg.log = false; //por default es true, si es falso solo logueamos el comando pero no la respuesta, para no saturar el log con respuestas largas de comandos como help o dir o more, pero igual queremos que se procesen y se puedan usar
        }
        dispatcher.submit(msg);
        return;
    }

    if (tokens[0].arg.empty() && tokens[1].arg.empty()) {
        std::string comando = tokens[0].name;
        std::string args_concat;

        for (size_t i = 1; i < tokens.size(); i++) {
            if (!args_concat.empty()) args_concat += " ";
            args_concat += tokens[i].name;
        }

        //printf("Process_commands: comando='%s' args='%s'\n", comando.c_str(), args_concat.c_str());

        cmd_msg_t msg{};
        msg.src = src;
        msg.client_fd = client_fd;
        snprintf(msg.name, sizeof(msg.name), "%s", comando.c_str());
        snprintf(msg.arg, sizeof(msg.arg), "%s", args_concat.c_str());

        if (strcmp(msg.name, "help") == 0 ||
            strcmp(msg.name, "dir") == 0 ||
            strcmp(msg.name, "webupdate") == 0 ||
            strcmp(msg.name, "more") == 0) 
        {
            msg.log = false; //por default es true, si es falso solo logueamos el comando pero no la respuesta, para no saturar el log con respuestas largas de comandos como help o dir o more, pero igual queremos que se procesen y se puedan usar
        }

        dispatcher.submit(msg);
        return;
    }

    for (auto &pt : tokens) {
        cmd_msg_t msg{};
        msg.src = src;
        msg.client_fd = client_fd;
        snprintf(msg.name, sizeof(msg.name), "%s", pt.name.c_str());
        snprintf(msg.arg, sizeof(msg.arg), "%s", pt.arg.c_str());
        if (strcmp(msg.name, "help") == 0 ||
            strcmp(msg.name, "dir") == 0 ||
            strcmp(msg.name, "webupdate") == 0 ||
            strcmp(msg.name, "more") == 0) 
        {
            msg.log = false; //por default es true, si es falso solo logueamos el comando pero no la respuesta, para no saturar el log con respuestas largas de comandos como help o dir o more, pero igual queremos que se procesen y se puedan usar
        }
        //printf("Process_commands (multi): comando='%s' arg='%s'\n",  msg.name, msg.arg);

        dispatcher.submit(msg);
    }
}
