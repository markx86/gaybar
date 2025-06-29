#include <bar.h>
#include <wl.h>
#include <compiler.h>

#include <time.h>

static inline void get_time(struct timespec* tm) {
  clock_gettime(CLOCK_MONOTONIC, tm);
}

static void render(void) {
  wl_clear(COLORU32(255, 255, 255));
}

static int tick(f32 delta) {
  UNUSED(delta);
  return 0;
}

int bar_init(enum bar_position position, u32 size) {
  return wl_init(position, size);
}

int bar_loop(void) {
  int rc;
  struct timespec now, prev;
  f32 delta;

  get_time(&prev);

  while (!wl_should_close()) {
    get_time(&now);
    delta = (f32)(now.tv_sec - prev.tv_sec) +
            (f32)(now.tv_nsec - prev.tv_nsec) / 1e9;
    prev = now;

    rc = tick(delta);
    if (rc < 0)
      goto fail;

    if (wl_draw_begin()) {
      render();
      wl_draw_end();
    }
  }

  rc = 0;
fail:
  return rc;
}

void bar_cleanup(void) {
  wl_cleanup();
}
