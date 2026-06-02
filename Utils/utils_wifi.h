#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H
#include <string>  //Tiene que estar antes de extern "C"

#ifdef __cplusplus
extern "C" {
#endif

// Declaración de la función principal de inicialización Wi-Fi
// --- SEMÁFORO Y ESTADO GLOBAL ---
// Definición de posibles estados de conexión
/*
cómo funcionan los enumeradores (enum) en C, que son un concepto fundamental.💡 
¿Qué es un enum en C?
Un enum (enumerador) es un tipo de dato definido por el usuario que consiste en un conjunto de constantes enteras nombradas. 
Su propósito principal es mejorar la legibilidad y el mantenimiento del código, 
permitiéndote usar nombres significativos en lugar de números "mágicos".En tu código:Ctypedef enum {
    WIFI_MODE_NONE = 0,
    WIFI_MODE_STA_CONNECTED,
    WIFI_MODE_AP_STARTED,
    WIFI_MODE_DISCONNECTED
} wifi_state_t;
Estás creando un nuevo tipo llamado wifi_state_t cuyos posibles valores son los nombres que listaste.
1. 🤔 ¿Por qué no se usa char, int, etc.?No se utilizan tipos de datos (como char, int, o float) dentro del enum 
porque todos los elementos de un enumerador son automáticamente tratados como constantes de tipo entero (int) 
por el compilador de C.
El enum sirve como un mapa de nombres a números, y esos números siempre serán enteros.
WIFI_MODE_NONE es simplemente un nombre para el número 0.
WIFI_MODE_STA_CONNECTED es un nombre para el número 1.Y así sucesivamente.
2. 🔢 ¿Por qué no se les da un valor inicial a todos?
Puedes dar un valor inicial al primer elemento, pero no es necesario para todos porque 
el compilador asigna valores automáticamente.
El comportamiento por defecto es el siguiente:
Si el primer elemento no tiene valor asignado, el compilador le asigna automáticamente el valor 0.
A partir de ese punto, cada elemento sucesivo recibe el valor del elemento anterior más uno.
Desglose de tu enum:
Constante NombradaValor Asignado ¿Por qué?
WIFI_MODE_NONE 0 Asignado explícitamente en el código.
WIFI_MODE_STA_CONNECTED 1 Valor anterior (0) más uno.
WIFI_MODE_AP_STARTED 2 Valor anterior (1) más uno.
WIFI_MODE_DISCONNECTED 3 Valor anterior (2) más uno.
*/

/*
Cabecera	Tipo de dato principal	Funciones típicas	Lenguaje
<string>	std::string	s.append(), s.substr(), +	C++
<string.h>	char[] o char*	strcpy, strlen, strcmp	C
<cstring>	char*	Igual que string.h	C++ (forma recomendada)
*/
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h" //para crear la tarea de reconexión 
/*
typedef enum {
    MY_WIFI_MODE_NONE = 0,
    MY_WIFI_MODE_STA_CONNECTED,
    MY_WIFI_MODE_AP_STARTED,
    MY_WIFI_MODE_DISCONNECTED
} wifi_state_t;
*/

void wifi_hardware_init(void); //inicializa el hardware wifi sin conectar
// Función para iniciar la conexión Wi-Fi con SSID y contraseña dinámicos
bool conectar_wifi(const char* ssid, const char* pass, int timeout_ms = 10000);
void iniciar_modo_ap(const char* ssid, const char* pass);


// Función pública para que otras tareas lean el estado:
//wifi_state_t get_wifi_state(void);
/*
wifi_mode_t modo;
esp_wifi_get_mode(&modo); 
if (modo == WIFI_MODE_AP) { ... }
*/
//static void set_wifi_state(wifi_state_t new_state);
extern void mqtt_app_stop(void);
// Bits para el grupo de eventos
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
extern EventGroupHandle_t s_wifi_event_group;

#ifdef __cplusplus
}
#endif
extern int s_retry_num;
extern bool wifi_ready;
extern bool g_manual_wifi_connect;
//extern bool g_ota_en_progreso;

extern EventGroupHandle_t s_wifi_event_group;

//estas funciones son C no C++ por eso deben ir fuera del extern "C"
std::string get_local_ip() ;
std::string get_connected_ssid() ;
std::string get_mac_address() ;
void save_wifi_network(const char* ssid, const char* pass);
bool conectar_wifi_desde_json();
bool iniciar_proceso_conexion_maestra();
bool esperar_conexion(int timeout_ms);
void wifi_check_task(void *pvParameter);
bool delete_wifi_network(const char* ssid_to_delete);

extern TaskHandle_t wifi_connect_task_handle;

void wifi_connect_task(void*);
#endif // WIFI_SETUP_H