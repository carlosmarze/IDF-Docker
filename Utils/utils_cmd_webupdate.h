#pragma once
#include <string>
#include <vector>

std::vector<std::string> parse_dnld_list(const std::string& body);
void ensure_dirs(const char* path);
int http_get_remote_size(const std::string& url);
bool should_download(const char* local_path, int remote_size, bool refresh);
bool download_file(const std::string& url, const char* local_path);
bool process_file(const std::string& base_url,
                  const std::string& remote_path,
                  bool refresh);

// acá exponés solo la función de registro
class CommandDispatcher;   // forward declaration
void register_update_web_command(CommandDispatcher& dispatcher);
