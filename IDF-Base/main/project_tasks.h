#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void start_project_tasks();

#ifdef __cplusplus
}
#endif

//Poner acá las tareas que deben estar en loop dentro del proyecto, para que el main quede lo más limpio posible, y para tener todas las tareas del proyecto organizadas en un solo archivo. Estas tareas pueden ser de ejemplo, o pueden ser las tareas reales del proyecto, según convenga. La idea es que el main se encargue de la inicialización y el arranque de las tareas, pero que la lógica de las tareas esté en este archivo, para mantener el main lo más limpio posible y para tener una mejor organización del código.