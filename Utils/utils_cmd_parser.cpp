#include <string>
#include <vector>
#include <cJSON.h>

struct ParsedToken {
    std::string name;
    std::string arg;
};

// ============================================================
// 1. PARSER JSON PURO (cJSON)
// ============================================================

static std::vector<ParsedToken> parse_json(const std::string& line) {
    std::vector<ParsedToken> out;

    cJSON* root = cJSON_Parse(line.c_str());
    if (!root) return out;

    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cmd || !cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return out;
    }

    std::string comando = cmd->valuestring;

    // Caso arg simple
    cJSON* arg = cJSON_GetObjectItem(root, "arg");
    if (arg && cJSON_IsString(arg)) {
        out.push_back({comando, arg->valuestring});
        cJSON_Delete(root);
        return out;
    }

    // Caso arg diccionario
    if (arg && cJSON_IsObject(arg)) {
        out.push_back({comando, ""});

        cJSON* child = arg->child;
        while (child) {
            if (cJSON_IsString(child))
                out.push_back({child->string, child->valuestring});
            else if (cJSON_IsNumber(child))
                out.push_back({child->string, std::to_string(child->valuedouble)});
            child = child->next;
        }

        cJSON_Delete(root);
        return out;
    }

    // Caso args array
    cJSON* args = cJSON_GetObjectItem(root, "args");
    if (args && cJSON_IsArray(args)) {
        out.push_back({comando, ""});

        int n = cJSON_GetArraySize(args);
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(args, i);
            if (cJSON_IsString(item))
                out.push_back({comando, item->valuestring});
        }

        cJSON_Delete(root);
        return out;
    }

    cJSON_Delete(root);
    return out;
}

// ============================================================
// 2. TOKENIZADOR DE COMILLAS (NO JSON)
// ============================================================

static std::vector<std::string> tokenize_quoted(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];

        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }

        if (c == ' ' && !in_quotes) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }

        cur.push_back(c);
    }

    if (!cur.empty()) out.push_back(cur);

    return out;
}

// ============================================================
// 3. PARSER PRINCIPAL
// ============================================================

#include <string>
#include <vector>
#include <cctype>

struct Token {
    std::string name;
    std::string arg;
};

// Trim helper
static std::string trim(const std::string& str) {
    size_t start = 0;
    size_t end = str.length();
    
    while (start < end && std::isspace(str[start])) start++;
    while (end > start && std::isspace(str[end - 1])) end--;
    
    return str.substr(start, end - start);
}

// Parser principal
std::vector<Token> parse_lineX(const std::string& line) {
    std::vector<Token> tokens;
    std::string trimmed = trim(line);
    
    if (trimmed.empty()) {
        return tokens;
    }
    
    size_t pos = 0;
    size_t len = trimmed.length();
    
    while (pos < len) {
        // Saltar espacios
        while (pos < len && std::isspace(trimmed[pos])) pos++;
        if (pos >= len) break;
        
        Token token;
        
        // Detectar si es key=value
        size_t key_start = pos;
        size_t eq_pos = std::string::npos;
        size_t space_pos = std::string::npos;
        
        // Buscar '=' y espacio en este token
        while (pos < len && !std::isspace(trimmed[pos])) {
            if (trimmed[pos] == '=' && eq_pos == std::string::npos) {
                eq_pos = pos;
            }
            pos++;
        }
        space_pos = pos;
        
        if (eq_pos != std::string::npos && eq_pos > key_start) {
            // Formato key=value
            token.name = trimmed.substr(key_start, eq_pos - key_start);
            
            // Saltar el '='
            pos = eq_pos + 1;
            
            // Leer el valor
            if (pos < len && trimmed[pos] == '"') {
                // Valor entre comillas
                pos++; // Saltar comilla inicial
                size_t value_start = pos;
                while (pos < len && trimmed[pos] != '"') {
                    if (trimmed[pos] == '\\' && pos + 1 < len) {
                        pos++; // Saltar escape
                    }
                    pos++;
                }
                token.arg = trimmed.substr(value_start, pos - value_start);
                if (pos < len) pos++; // Saltar comilla final
            } else {
                // Valor sin comillas
                size_t value_start = pos;
                while (pos < len && !std::isspace(trimmed[pos])) {
                    pos++;
                }
                token.arg = trimmed.substr(value_start, pos - value_start);
            }
        } else {
            // Formato posicional (sin '=')
            pos = key_start; // Resetear posición
            
            if (trimmed[pos] == '"') {
                // Argumento entre comillas
                pos++; // Saltar comilla inicial
                size_t value_start = pos;
                while (pos < len && trimmed[pos] != '"') {
                    if (trimmed[pos] == '\\' && pos + 1 < len) {
                        pos++; // Saltar escape
                    }
                    pos++;
                }
                token.name = trimmed.substr(value_start, pos - value_start);
                if (pos < len) pos++; // Saltar comilla final
            } else {
                // Argumento sin comillas
                size_t value_start = pos;
                while (pos < len && !std::isspace(trimmed[pos])) {
                    pos++;
                }
                token.name = trimmed.substr(value_start, pos - value_start);
            }
            token.arg = ""; // Sin argumento
        }
        
        tokens.push_back(token);
    }
    
    return tokens;
}