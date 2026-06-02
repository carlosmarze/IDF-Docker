#pragma once

typedef void (*sched_cb_t)();

void start_scheduler();

// Registrar callbacks
void sched_register_task_60(sched_cb_t cb);
void sched_register_task_3600_post(sched_cb_t cb);
void sched_register_task_3600_updt(sched_cb_t cb);
