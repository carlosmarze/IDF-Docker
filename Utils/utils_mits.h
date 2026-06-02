#ifndef UTILS_MITS_H
#define UTILS_MITS_H
 #ifdef __cplusplus
        extern "C"  {
    #endif
    
#include "freertos/FreeRTOS.h"
struct TSmessageWrite {
    char write_api_key[20];
    char esquema[10];
    int sensor_id;
    char field_name[8][8]; // hasta 8 nombre decampos, cada uno con hasta 7 caracteres + null terminator
    char field_data[7][64]; // hasta 7 campos, cada uno con hasta 63 caracteres + null terminator
    char field_data8[512]; // 8vo campo más grande para estado u otros datos
   
};
extern struct TSmessageWrite mensajeWrite; //la defino en utils_mits.cpp porque si no da error de redefinición cada vez que se incluye este .h
struct TSmessageRead {
    char read_api_key[20];
    char esquema[10];
    int sensor_id;
    char field_name[8][8]; // hasta 8 nombre decampos, cada uno con hasta 7 caracteres + null terminator
    char field_data[7][64]; // hasta 7 campos, cada uno con hasta 63 caracteres + null terminator
    char field_data8[512]; // 8vo campo más grande para estado u otros datos
   
};
extern struct TSmessageRead mensajeRead; //la defino en utils_mits.cpp porque si no da error de redefinición cada vez que se incluye este .h
void WriteTSBulk(TSmessageWrite *mensaje,  char *_miURL, char* resultado_buf, int size_resultado_buf); //WriteTS(stmensaje, "miTSESP.com", resultado_buf)
void miTS_init();
bool writePost(
    const char* url,
    std::initializer_list<std::pair<const char*, const char*>> fields,
    TickType_t timeout_ms = 2000
);

#ifdef __cplusplus
        }
    #endif
#endif // UTILS_MITS_H
/**********************************************
 * Uso de writePost
 * ******************************************
#include "utils_miTS.h"

writePost(
    g_tsurl,
    {
        {"field1", "Hola"},
        {"field3", "123"},
        {"field8", "Status=OK"}
    },
    3000
);
*/