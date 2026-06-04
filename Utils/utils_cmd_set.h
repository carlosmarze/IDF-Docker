//#pragma once
#ifndef UTILS_CMD_SET_H
#define UTILS_CMD_SET_H

#include <stddef.h>
#include <stdbool.h>
#include <string>   // incluir fuera del extern "C"
#include "esp_http_client.h" // Se usa para el tipo esp_http_client_handle_t
#include "esp_err.h"
#include "utils_cmd_dispatcher.h"  // para CommandDispatcher

// Si querés que estas funciones se puedan llamar desde C puro,
// NO deben usar std::string en la firma. En ese caso, usá const char*.
// Si solo las vas a usar desde C++, podés dejarlas con std::string.

#ifdef __cplusplus
extern "C" {
#endif

// Si querés compatibilidad con C, usá esto:
// void setSSID(const char *ssid);
// void setWifiPass(const char *pass);
// void setMiTSServer(const char *tsServer);

#ifdef __cplusplus
}
#endif
//extern CommandDispatcher& dispatcher; //declaración del dispatcher global
// Si solo las vas a usar desde C++, declaralas aquí fuera del extern "C":
void setSSID(const std::string &ssid);
void setWifiPass(const std::string &pass);
void setMiTSServer(const std::string &tsServer);

void register_utils_commands(CommandDispatcher& dispatcher) ;
void save_config(const char* new_cmd);
#endif // UTILS_CMD_SET_H


