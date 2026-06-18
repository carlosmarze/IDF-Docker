#ifndef UTILS_TIME_H
#define UTILS_TIME_H
#include <string> //Tiene que estar antes de extern "C"

    #ifdef __cplusplus
        extern "C"  {
    #endif
#include <stddef.h> // Para size_t
/**
 * @brief Obtiene la fecha y hora actual formateada como string.
 * @param buffer Puntero al char array donde se escribirá el resultado.
 * @param max_len Tamaño máximo del buffer (ej: sizeof(buffer)).
 */

void get_fecha_hora(char *buffer, size_t max_len);
// Devuelve fecha y hora local en formato "YYYY-MM-DD HH:MM:SS", más eficienta que la anterior 
const char* get_datetime();

static void create_status_page(const char *status_cmd, char *buffer, size_t max_len);


#ifdef __cplusplus
        }
    #endif
std::string get_current_time(); //debe ir fuera del extern "C"

#endif // UTILS_TIME_H
