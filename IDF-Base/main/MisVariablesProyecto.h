#ifndef MISVARIABLESPROYECTO_H
    #define MISVARIABLESPROYECTO_H
    #define version_info "IDFBASE20260602_V1" // Definición de la versión del firmware
    extern int SensorID ; // Identificador único del sensor lo definiremos en main.cpp o donde le pongamos el valor (leido de file seguramente)
    extern char urlUpdate[128]; //url para el update OTA
    #define URL_BASE "carze.pythonanywhere.com" //url base
    #define urlUpdateDef "https://carze.pythonanywhere.com/update" //url base para el update OTA, sin parámetros
    #define URL_FILES_UPDATE "https://carze.pythonanywhere.com/field/html" //url para bajar archivos de configuración. Ejemplo: https://carze.pythonanywhere.com/field/html?api_key=REZAQ4BH81OQP9PZ&SensorID=25
    //url para bajar archivos de configuración. Ejemplo: https://carze.pythonanywhere.com/field/html?api_key=REZAQ4BH81OQP9PZ&SensorID=25
    #define ESQUEMA "ESP32IDF" // Esquema de datos para miTS, lo definimos como constante porque no cambia, pero podría ser una variable si se quisiera usar el mismo firmware para distintos esquemas.
    #define WRITE_API_KEY "WR4QL85BR9KIBP6V" // Write API Key para miTS, lo definimos como constante porque no cambia, pero podría ser una variable si se quisiera usar el mismo firmware para distintos sensores.
    #define READ_API_KEY "REZAQ4BH81OQP9PZ" // Read API Key para miTS, lo definimos como constante porque no cambia, pero podría ser una variable si se quisiera usar el mismo firmware para distintos sensores.
    
    #define SENSORID 7001 // SensorID para miTS, lo definimos temporariamente como constante pero luego debe sacarse de config
#endif