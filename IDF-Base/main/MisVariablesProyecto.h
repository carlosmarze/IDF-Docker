#ifndef MISVARIABLESPROYECTO_H
    #define MISVARIABLESPROYECTO_H
    #define version_info "HEATHER20260723_V1" // con IDF6 Para calefaccion pileta con sensores de temperatura y comunicacion con Home Assistant
    //extern int SensorID ; // Identificador único del sensor lo definiremos en main.cpp o donde le pongamos el valor (leido de file seguramente)
    //#define ESQUEMA "ESP32IDF" // Esquema de datos para miTS, lo definimos como constante porque no cambia, pero podría ser una variable si se quisiera usar el mismo firmware para distintos esquemas.
    #define WRITE_API_KEY "WR4QL85BR9KIBP6V" // Write API Key para miTS defaults, cuando no existe archivo config.
    #define READ_API_KEY "REZAQ4BH81OQP9PZ" // Read API Key para miTS, lo definimos como constante porque no cambia, pero podría ser una variable si se quisiera usar el mismo firmware para distintos sensores.
    #define SENSORID 0001 // SensorID DEFAULT
#endif