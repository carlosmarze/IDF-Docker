#include <string>
#include <vector>
#include <sstream>
#include <cctype>

struct ParsedToken {
    std::string name;
    std::string arg;
};

// ============================================================
// Helpers
// ============================================================

static std::string trimX(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string unescapeX(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool esc = false;

    for (char c : s) {
        if (esc) {
            out.push_back(c);
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static std::string handle_double_double_quotesX(const std::string &s) {
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '"' && i + 1 < s.size() && s[i+1] == '"') {
            out.push_back('"');
            i++;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

static std::string remove_outer_quotesX(const std::string &s) {
    std::string t = trimX(s);

    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        std::string inner = t.substr(1, t.size() - 2);

        std::string out;
        out.reserve(inner.size());
        bool esc = false;

        for (char c : inner) {
            if (esc) {
                out.push_back(c);
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else {
                out.push_back(c);
            }
        }

        return out;
    }

    if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') {
        return t.substr(1, t.size() - 2);
    }

    if (!t.empty() && t.front() == '"') {
        return t.substr(1);
    }

    return unescapeX(t);
}

// ============================================================
// JSON-like detection
// ============================================================

static bool is_json_likeX(const std::string &s) {
    std::string t = trimX(s);
    return !t.empty() && t.front() == '{' && t.back() == '}';
}

// ============================================================
// JSON-like parser EXTENDIDO
// ============================================================

static std::vector<ParsedToken> parse_json_likeX(const std::string &line) {
    std::vector<ParsedToken> out;

    std::string s = trimX(line.substr(1, line.size() - 2));

    std::vector<std::string> pairs;
    std::string cur;
    bool in_single = false, in_double = false, esc = false;

    for (char c : s) {
        if (esc) { cur.push_back(c); esc = false; continue; }
        if (c == '\\') { esc = true; cur.push_back(c); continue; }
        if (c == '"' && !in_single) in_double = !in_double;
        if (c == '\'' && !in_double) in_single = !in_single;

        if (c == ',' && !in_single && !in_double) {
            pairs.push_back(trimX(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) pairs.push_back(trimX(cur));

    std::string comando;
    std::string arg_raw;

    for (auto &p : pairs) {
        size_t pos = p.find(':');
        if (pos == std::string::npos) continue;

        std::string key = trimX(p.substr(0, pos));
        std::string val = trimX(p.substr(pos + 1));

        key = remove_outer_quotesX(key);

        if (key == "cmd") {
            comando = remove_outer_quotesX(val);
        }
        else if (key == "arg") {
            arg_raw = trimX(val);
        }
    }

    if (comando.empty()) return out;

    if (!arg_raw.empty() && arg_raw.front() != '[' && arg_raw.front() != '{') {
        std::string val = remove_outer_quotesX(arg_raw);
        out.push_back({comando, val});
        return out;
    }

    if (!arg_raw.empty() && arg_raw.front() == '[') {
        std::string inner = trimX(arg_raw.substr(1, arg_raw.size() - 2));

        std::vector<std::string> kvs;
        std::string cur2;
        in_single = in_double = esc = false;

        for (char c : inner) {
            if (esc) { cur2.push_back(c); esc = false; continue; }
            if (c == '\\') { esc = true; cur2.push_back(c); continue; }
            if (c == '"' && !in_single) in_double = !in_double;
            if (c == '\'' && !in_double) in_single = !in_single;

            if (c == ',' && !in_single && !in_double) {
                kvs.push_back(trimX(cur2));
                cur2.clear();
            } else {
                cur2.push_back(c);
            }
        }
        if (!cur2.empty()) kvs.push_back(trimX(cur2));

        out.push_back({comando, ""});

        for (auto &kv : kvs) {
            size_t pos = kv.find(':');
            if (pos == std::string::npos) continue;

            std::string k = trimX(kv.substr(0, pos));
            std::string v = trimX(kv.substr(pos + 1));

            k = remove_outer_quotesX(k);
            v = remove_outer_quotesX(v);

            out.push_back({k, v});
        }

        return out;
    }

    if (!arg_raw.empty() && arg_raw.front() == '{') {
        std::string inner = trimX(arg_raw.substr(1, arg_raw.size() - 2));

        std::vector<std::string> kvs;
        std::string cur2;
        in_single = in_double = esc = false;

        for (char c : inner) {
            if (esc) { cur2.push_back(c); esc = false; continue; }
            if (c == '\\') { esc = true; cur2.push_back(c); continue; }
            if (c == '"' && !in_single) in_double = !in_double;
            if (c == '\'' && !in_double) in_single = !in_single;

            if (c == ',' && !in_single && !in_double) {
                kvs.push_back(trimX(cur2));
                cur2.clear();
            } else {
                cur2.push_back(c);
            }
        }
        if (!cur2.empty()) kvs.push_back(trimX(cur2));

        out.push_back({comando, ""});

        for (auto &kv : kvs) {
            size_t pos = kv.find(':');
            if (pos == std::string::npos) continue;

            std::string k = trimX(kv.substr(0, pos));
            std::string v = trimX(kv.substr(pos + 1));

            k = remove_outer_quotesX(k);
            v = remove_outer_quotesX(v);

            out.push_back({k, v});
        }

        return out;
    }

    return out;
}

// ============================================================
// Autodetección robusta
// ============================================================

static void detect_separatorsX(const std::string &line, char &sep1, char &sep2) {
    std::string t = trimX(line);

    if (t.find(',') != std::string::npos) {
        sep1 = ',';
        sep2 = '=';
        return;
    }

    if (t.find('=') != std::string::npos) {
        sep1 = ' ';
        sep2 = '=';
        return;
    }

    if (t.find(':') != std::string::npos) {
        sep1 = ' ';
        sep2 = ':';
        return;
    }

    sep1 = ' ';
    sep2 = '=';
}

// ============================================================
// Split tokens
// ============================================================

static std::vector<std::string> split_tokensX(const std::string &line, char sep1) {
    std::vector<std::string> out;
    std::string cur;
    bool in_single = false, in_double = false, esc = false;

    for (char c : line) {
        if (esc) { cur.push_back(c); esc = false; continue; }
        if (c == '\\') { esc = true; cur.push_back(c); continue; }
        if (c == '"' && !in_single) in_double = !in_double;
        if (c == '\'' && !in_double) in_single = !in_single;

        if (c == sep1 && !in_single && !in_double) {
            out.push_back(trimX(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(trimX(cur));

    return out;
}

// ============================================================
// PARSER PRINCIPAL
// ============================================================

std::vector<ParsedToken> parse_lineX(const std::string &line) {
    std::vector<ParsedToken> result;
    std::string clean = trimX(line);

    if (clean.empty() || clean[0] == '#')
        return result;

    if (is_json_likeX(clean))
        return parse_json_likeX(clean);

    char sep1, sep2;
    detect_separatorsX(clean, sep1, sep2);

    auto tokens = split_tokensX(clean, sep1);

    for (auto &t : tokens) {
        size_t pos = t.find(sep2);
        if (pos == std::string::npos) {
            result.push_back({t, ""});
            continue;
        }

        std::string key = trimX(t.substr(0, pos));
        std::string val = trimX(t.substr(pos + 1));

        val = remove_outer_quotesX(val);

        result.push_back({key, val});
    }

    return result;
}
