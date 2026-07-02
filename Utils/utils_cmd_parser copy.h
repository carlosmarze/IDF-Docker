#pragma once
#include <string>
#include <vector>

//
// ParsedToken: resultado de cada par key/val
//
/*struct ParsedToken {
    std::string name;
    std::string arg;
};*/

//
// parse_line()
//  - Autodetecta formato:
//      1) key=val,key2=val2        → sep1=',', sep2='='
//      2) key=val key2=val2        → sep1=' ', sep2='='
//      3) key:val key2:val2        → sep1=' ', sep2=':'
//      4) {"key":"val", ...}       → JSON-like, ignora sep1/sep2
//
//  - Soporta comillas simples y dobles
//  - Soporta escapes: \"  \'  
//  - Soporta comillas dobles internas estilo CSV: "" → "
//  - Ignora líneas vacías o que comienzan con '#'
//
//  Retorna vector<ParsedToken> con todos los pares parseados.
//
std::vector<ParsedToken> parse_lineX(const std::string &line);

