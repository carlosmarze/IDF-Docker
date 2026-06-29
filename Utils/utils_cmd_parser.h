#ifndef UTILS_CMD_PARSER_H
#define UTILS_CMD_PARSER_H

#include <string>
#include <vector>

// ------------------------------------------------------------
// Estructura base del parser
// ------------------------------------------------------------
struct ParsedToken {
    std::string name;   // nombre del comando o clave
    std::string arg;    // argumento asociado
};

// ------------------------------------------------------------
// Función principal del parser
// ------------------------------------------------------------
// Soporta:
//   {"cmd":"setssid","arg":"DepartamentoJ"}
//   {"cmd":"ota","arg":{"server":"x","chunksize":1024}}
//   {"cmd":"setwifi","args":["Departamento J","el departamento"]}
//   setwifi "Departamento J" "el departamento"
//   ota server=http://x chunksize=2048 reboot=no
//
// Devuelve un vector de ParsedToken:
//   - tokens[0].name = comando
//   - tokens[i].arg  = argumentos posicionales o pares key/value
// ------------------------------------------------------------
std::vector<ParsedToken> parse_lineX(const std::string& line);

#endif // UTILS_CMD_PARSER_H
