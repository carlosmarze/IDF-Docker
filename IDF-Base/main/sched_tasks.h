#pragma once

//void sched_tasks_init();   // para inicializar estructuras compartidas

void task_60();            // GET keepalive + TSComm
void task_60_temp();       // lectura periódica de temperatura DS18B20
void task_3600_post();     // POST de estado
void task_3600_updt();     // status + OTA
void set_sched_inicio(bool esinicio = false); // para setear el estado de "primera ejecución" desde main o desde la tarea misma, según convenga
