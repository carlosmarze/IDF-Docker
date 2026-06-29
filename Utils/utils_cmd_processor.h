#ifndef UTILS_CMD_PROCESSOR_H
#define UTILS_CMD_PROCESSOR_H

#include <string>
#include "utils_cmd_dispatcher.h"  // porque usa cmd_msg_t y CommandDispatcher
#include "utils_cmd_parser.h"
// Procesa una línea de texto crudo y envía los comandos al dispatcher
void process_commands(cmd_source_t src,
                      const std::string &line,
                      char sep1 = ' ',   // separador entre comandos
                      char sep2 = '=',    // separador nombre/argumento
                      int client_fd = -1  // descriptor de cliente para respuestas asíncronas
);

// Función auxiliar: recibe una línea y separadores
static std::string remove_outer_quotes(const std::string &s);
std::vector<ParsedToken> parse_line(const std::string &line,
                                    char sep1,   // separador entre comandos o parámetros
                                    char sep2);   // separador entre nombre y argumento
std::vector<ParsedToken> parse_line_old(const std::string &line,
                                    char sep1,   // separador entre comandos o parámetros
                                    char sep2);   // separador entre nombre y argumento

#endif // CMD_PROCESSOR_H