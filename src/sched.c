#include <gaybar/sched.h>
#include <gaybar/assert.h>
#include <gaybar/list.h>
#include <gaybar/util.h>

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

struct task {
  u64 id;
  struct list link;
  struct timespec execute_time;
  size_t interval;
  task_t execute;
};

static struct list g_task_list;
static struct list g_task_queue;
static timer_t g_timer;
static b8 g_timer_expired;
static u64 g_next_id;

static void alrm_handler(int signo) {
  ASSERT(signo == SIGALRM);
  g_timer_expired = true;
}

static void set_alrm_handler(void) {
  struct sigaction sigact;
  sigact.sa_flags = 0;
  sigact.sa_handler = &alrm_handler;
  sigemptyset(&sigact.sa_mask);
  ASSERT(sigaction(SIGALRM, &sigact, NULL) == 0);
}

static void restore_alrm_handler(void) {
  struct sigaction sigact;
  sigact.sa_flags = 0;
  sigact.sa_handler = SIG_DFL;
  sigemptyset(&sigact.sa_mask);
  ASSERT(sigaction(SIGALRM, &sigact, NULL) == 0);
}

static void set_alarm_for(struct timespec* timestamp) {
  struct itimerspec ts;
  ts.it_value = *timestamp;
  ts.it_interval.tv_sec = ts.it_interval.tv_nsec = 0;
  g_timer_expired = false;
  if (timer_settime(g_timer, TIMER_ABSTIME, &ts, NULL))
    log_fatal("could not set scheduler timer: %m");
}

static void task_destroy(struct task* task) {
  list_remove(&task->link);
  free(task);
}

static void task_enqueue(struct task* task) {
  list_remove(&task->link);
  list_insert(&g_task_queue, &task->link);
}

void sched_init(void) {
  ASSERT(timer_create(CLOCK_MONOTONIC, (void*)SIGEV_SIGNAL, &g_timer) == 0);
  list_init(&g_task_list);
  list_init(&g_task_queue);
  set_alrm_handler();
  g_timer_expired = true;
  g_next_id = 0;
}

void sched_cleanup(void) {
  struct task *task, *task_next;

  list_for_each_safe(task, task_next, &g_task_list, link)
    task_destroy(task);

  list_for_each_safe(task, task_next, &g_task_queue, link)
    task_destroy(task);

  restore_alrm_handler();
}

/* Returns +1 if the time indicated by a comes after the time indicated by b,
 * -1 if it comes before and 0 if the timevals are equal.
 */
static int timespec_cmp(struct timespec* a, struct timespec* b) {
  i64 sec_diff, nsec_diff;

  sec_diff = a->tv_sec - b->tv_sec;
  nsec_diff = a->tv_nsec - b->tv_nsec;

  if (sec_diff == 0)
    return signi(nsec_diff);
  else
    return signi(sec_diff);
}

void sched_queue_prepare(void) {
  int rc;
  struct task *task, *task_next;
  struct timespec timeout;

  if (!g_timer_expired)
    return;

  timeout.tv_sec = timeout.tv_nsec = INT64_MAX;

  list_for_each(task, &g_task_list, link) {
    rc = timespec_cmp(&timeout, &task->execute_time);
    if (rc > 0)
      timeout = task->execute_time;
  }

  list_for_each_safe(task, task_next, &g_task_list, link) {
    rc = timespec_cmp(&timeout, &task->execute_time);
    if (rc >= 0)
      task_enqueue(task);
  }

  if (!list_empty(&g_task_queue))
    set_alarm_for(&timeout);
}

static void get_execute_time(struct timespec* timespec, size_t delay_ms) {
  size_t secs, nsecs;
  struct timespec tm;

  monotonic_time(&tm);
  secs = delay_ms / 1000;
  nsecs = (delay_ms % 1000) * 1e6;

  timespec->tv_nsec = tm.tv_nsec + nsecs;
  if (timespec->tv_nsec >= 1e9) {
    timespec->tv_nsec -= 1e9;
    ++secs;
  }
  timespec->tv_sec = tm.tv_sec + secs;
}

void sched_queue_run(void) {
  struct task *task, *task_next;

  if (!g_timer_expired || list_empty(&g_task_queue))
    return;

  list_for_each_safe(task, task_next, &g_task_queue, link) {
    list_remove(&task->link);
    task->execute();
    if (task->interval == 0)
      free(task);
    else {
      get_execute_time(&task->execute_time, task->interval);
      list_insert(&g_task_list, &task->link);
    }
  }
}

static u64 create_task(task_t task, size_t interval_ms, size_t delay_ms) {
  struct task* task_struct = malloc(sizeof(*task_struct));
  ASSERT(task_struct != NULL);
  task_struct->id = g_next_id++;
  task_struct->execute = task;
  task_struct->interval = interval_ms;
  get_execute_time(&task_struct->execute_time, delay_ms);
  list_insert(&g_task_list, &task_struct->link);
  return task_struct->id;
}

u64 sched_task_delayed(task_t task, size_t delay_ms) {
  return create_task(task, 0, delay_ms);
}

u64 sched_task_interval(task_t task, size_t interval_ms, b8 run_immediately) {
  return create_task(task, interval_ms, run_immediately ? 0 : interval_ms);
}

void sched_task_delete(u64 id) {
  struct task *task;
  ASSERT(id < g_next_id);

  list_for_each(task, &g_task_queue, link) {
    if (task->id == id)
      goto task_found;
  }
  list_for_each(task, &g_task_list, link) {
    if (task->id == id)
      goto task_found;
  }

  return;
task_found:
  task_destroy(task);
}
