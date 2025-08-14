#ifndef SCHED_H_
#define SCHED_H_

#include <gaybar/types.h>

typedef void (*task_t)(void);

void sched_init(void);
void sched_cleanup(void);

void sched_queue_prepare(void);
void sched_queue_run(void);

u64  sched_task_delayed(task_t task, size_t delay_ms);
u64  sched_task_interval(task_t task, size_t interval_ms, b8 run_immediately);
void sched_task_delete(u64 id);

#endif
