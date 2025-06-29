#include <bar.h>
#include <log.h>
#include <wl.h>
#include <compiler.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <signal.h>
#include <wchar.h>
#include <poll.h>
#include <errno.h>
#include <sys/mman.h>

#include <wayland/wlr-layer-shell-unstable-v1.h>
#include <wayland/xdg-output-unstable-v1.h>
#include <wayland-client.h>

STATICASSERT(sizeof(wchar_t) == sizeof(u32));

struct output {
  struct output *next, *prev;
  struct wl_output* wl_output;
  struct wl_surface* wl_surface;
  struct wl_callback* wl_callback;
  struct wl_buffer* wl_buffer;
  struct zxdg_output_v1* xdg_output;
  struct zwlr_layer_surface_v1* wlr_layer_surface;
  u32 id;
  char* name;
  char* description;
  i32 x, y;
  u32 width, height;
  u32 surface_width, surface_height;
  u32* buffer;
  size_t buffer_size;
  size_t buffer_stride;
  b8 frame_done;
};

struct wl {
  struct wl_display* wl_display;
  struct wl_registry* wl_registry;
  struct wl_compositor* wl_compositor;
  struct wl_shm* wl_shm;
  struct zxdg_output_manager_v1* xdg_output_manager;
  struct zwlr_layer_shell_v1* wlr_layer_shell;
  struct output* outputs;
  i32 display_fd;
  u32 output_format;
  b8 init_done, can_draw;
};

struct bar {
  enum zwlr_layer_surface_v1_anchor anchor;
  u32 size;
};

struct wl wl = {0};
struct bar bar = {0};
static b8 should_close = false;

#define FOREACHOUTPUT(x) \
  for (struct output* x = wl.outputs; x != NULL; x = x->next)

static void request_frame(struct output* output);

static inline i32 min(i32 x, i32 y) {
  return x < y ? x : y;
}

static inline i32 max(i32 x, i32 y) {
  return x > y ? x : y;
}

static inline i32 clamp(i32 x, i32 m, i32 M) {
  return min(max(m, x), M);
}

static inline char* output_name(struct output* output) {
  return output->name == NULL ? "UNK" : output->name;
}

static void wl_buffer_handle_release(void* data, struct wl_buffer* wl_buffer) {
  struct output* output = data;
  assert(wl_buffer == output->wl_buffer);
  log_trace("buffer released");
}

static const struct wl_buffer_listener wl_buffer_listener = {
  .release = wl_buffer_handle_release
};

static int create_buffer(struct output* output) {
  char buffer_name[32];
  i32 rc, fd;
  void* buffer;
  struct wl_shm_pool* wl_shm_pool;
  /*    u32 stride = output->surface_width * 4 */
  const u32 stride = output->surface_width << 2,
            size = stride * output->surface_height;

  snprintf(buffer_name, sizeof(buffer_name), "out-%u", output->id);

  rc = fd = syscall(SYS_memfd_create, buffer_name, 0);
  if (rc < 0)
    goto fail;
  rc = ftruncate(fd, size);
  if (rc < 0)
    goto fail;

  buffer = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buffer == MAP_FAILED) {
    rc = -1;
    goto fail;
  }

  output->buffer = buffer;
  output->buffer_size = size;
  output->buffer_stride = stride;

  wl_shm_pool = wl_shm_create_pool(wl.wl_shm, fd, size);
  output->wl_buffer = wl_shm_pool_create_buffer(wl_shm_pool, 0,
                                                output->surface_width,
                                                output->surface_height,
                                                stride, wl.output_format);
  wl_shm_pool_destroy(wl_shm_pool);

  wl_buffer_add_listener(output->wl_buffer, &wl_buffer_listener, output);
  rc = 0;
fail:
  if (fd >= 0)
    close(fd);
  return rc;
}

static void destroy_buffer(struct output* output) {
  if (output->wl_buffer != NULL) {
    wl_buffer_destroy(output->wl_buffer);
    output->wl_buffer = NULL;
  }
  if (output->buffer != NULL) {
    munmap(output->buffer, output->buffer_size);
    output->buffer = NULL;
  }
}

static inline void acquire_buffer(struct output* output) {
  while (!output->frame_done)
    wl_display_roundtrip(wl.wl_display);
}

static int recreate_buffer(struct output* output) {
  if (output->surface_width == 0 || output->surface_height == 0) {
    log_error("output %s (id: %u) has no width and no height",
              output_name(output), output->id);
    return -1;
  }
  /* Destroy previous buffer */
  destroy_buffer(output);
  /* Ensure the compositor is not using the buffer */
  acquire_buffer(output);
  /* Create buffer */
  if (create_buffer(output) < 0) {
    log_error("could not create buffer for output %s: %m", output_name(output));
    return -1;
  }
  /* The buffer is attached in the frame handler */
  return 0;
}

static inline void unlink_output(struct output* output) {
  if (output == wl.outputs)
    wl.outputs = output->next;
  if (output->next != NULL)
    output->next->prev = output->prev;
  if (output->prev != NULL)
    output->prev->next = output->next;
}

static void free_output(struct output* output) {
  if (output->name != NULL)
    free(output->name);
  if (output->description != NULL)
    free(output->description);
  free(output);
}

static void remove_output(struct output* output) {
  assert(output != NULL);
  log_trace("removing output %s", output_name(output));
  /* Remove the output from the linked list */
  unlink_output(output);
  /* Free wayland objects */
#define DESTROY(x) \
  do { if (output->x != NULL) x##_destroy(output->x); } while (0)
#define DESTROYUNSTABLE(x, v) \
  do { if (output->x != NULL) z##x##_v##v##_destroy(output->x); } while (0)

  DESTROYUNSTABLE(wlr_layer_surface, 1);
  DESTROY(wl_surface);
  destroy_buffer(output);
  DESTROYUNSTABLE(xdg_output, 1);
  DESTROY(wl_output);

#undef DESTROYUNSTABLE
#undef DESTROY
  /* Free heap objects */
  free_output(output);
}

static void xdg_output_handle_name(void* data,
                                   struct zxdg_output_v1* xdg_output,
                                   const char* name) {
  struct output* output = data;
  assert(xdg_output == output->xdg_output);
  output->name = strdup(name);
}

static void xdg_output_handle_description(void* data,
                                          struct zxdg_output_v1* xdg_output,
                                          const char* description) {
  struct output* output = data;
  assert(xdg_output == output->xdg_output);
  output->description = strdup(description);
}

static void
xdg_output_handle_logical_position(void* data,
                                   struct zxdg_output_v1* xdg_output,
                                   i32 x, i32 y) {
  struct output* output = data;
  assert(xdg_output == output->xdg_output);
  output->x = x;
  output->y = y;
}

static void
xdg_output_handle_logical_size(void* data,
                               struct zxdg_output_v1* xdg_output,
                               i32 width, i32 height) {
  struct output* output = data;
  assert(xdg_output == output->xdg_output);
  log_trace("got xdg output logical size (width = %d, height = %d)",
            width, height);
  output->width = width;
  output->height = height;
}

static void xdg_output_handle_done(void* data,
                                   struct zxdg_output_v1* xdg_output) {
  struct output* output = data;
  assert(xdg_output == output->xdg_output);
  log_trace("got done event for output %s", output_name(output));
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
  /* FIXME: xdg_output.name and xdg_output.description events are
   *        deprecated. We should use the wl_output's equivalents.
   */
  .name = xdg_output_handle_name,
  .description = xdg_output_handle_description,
  .logical_position = xdg_output_handle_logical_position,
  .logical_size = xdg_output_handle_logical_size,
  .done = xdg_output_handle_done
};

static void
wlr_layer_surface_handle_configure(
                                void* data,
                                struct zwlr_layer_surface_v1* wlr_layer_surface,
                                u32 serial, u32 width, u32 height) {
  struct output* output = data;
  assert(wlr_layer_surface == output->wlr_layer_surface);
  zwlr_layer_surface_v1_ack_configure(wlr_layer_surface, serial);

  if (output->surface_width == width && output->surface_height == height)
    return;

  output->surface_width = width;
  output->surface_height = height;

  /* Recreate the buffer */
  if (recreate_buffer(output) < 0)
    remove_output(output);
}

static void
wlr_layer_surface_handle_closed(
                              void* data,
                              struct zwlr_layer_surface_v1* wlr_layer_surface) {
  struct output* output = data;
  assert(wlr_layer_surface == output->wlr_layer_surface);
  should_close = true;
}

static const struct zwlr_layer_surface_v1_listener wlr_layer_surface_listener =
{
  .closed = wlr_layer_surface_handle_closed,
  .configure = wlr_layer_surface_handle_configure
};

static void wl_callback_frame_handle_done(void* data,
                                          struct wl_callback* wl_callback,
                                          u32 timestamp) {
  struct output* output = data;
  assert(wl_callback == output->wl_callback);
  UNUSED(timestamp);
  /* Clear the callback */
  wl_callback_destroy(wl_callback);
  output->wl_callback = NULL;
  /* Signal that this output is done with its frame */
  output->frame_done = true;
}

static const struct wl_callback_listener wl_callback_frame_listener = {
  .done = wl_callback_frame_handle_done
};

static void request_frame(struct output* output) {
  assert(output->wl_callback == NULL);
  /* Create the frame callback for the surface */
  output->wl_callback = wl_surface_frame(output->wl_surface);
  wl_callback_add_listener(output->wl_callback, &wl_callback_frame_listener,
                           output);
  wl_surface_commit(output->wl_surface);
  /* When a frame has been requested, we can't draw */
  output->frame_done = false;
}

static void init_output(struct output* output) {
  u32 initial_width, initial_height;

  /* Create xdg output */
  output->xdg_output =
    zxdg_output_manager_v1_get_xdg_output(wl.xdg_output_manager,
                                          output->wl_output);
  zxdg_output_v1_add_listener(output->xdg_output, &xdg_output_listener, output);

  /* Roundtrip to get output witdth and output height */
  wl_display_roundtrip(wl.wl_display);
  if (output->width == 0 || output->height == 0) {
    remove_output(output);
    return;
  }

  /* Compute bar size */
  switch (bar.anchor) {
    case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP:
    case ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM:
      assert(bar.size < output->height);
      initial_width = output->width;
      initial_height = bar.size;
      break;
    case ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT:
    case ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
      assert(bar.size < output->width);
      initial_width = bar.size;
      initial_height = output->height;
      break;
  }

  /* Create surface */
  output->wl_surface = wl_compositor_create_surface(wl.wl_compositor);

  /* Create wlr layer surface */
  output->wlr_layer_surface =
    zwlr_layer_shell_v1_get_layer_surface(wl.wlr_layer_shell,
                                          output->wl_surface,
                                          output->wl_output,
                                          /* NOTE: We use the background
                                           *       layer, because otherwise the
                                           *       bar is visible when the
                                           *       application is fullscreen
                                           */
                                          ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
                                          "gaybar");
  zwlr_layer_surface_v1_add_listener(output->wlr_layer_surface,
                                     &wlr_layer_surface_listener,
                                     output);

  /* Set wlr layer surface position, anchor, margin and exclusive zone */
  zwlr_layer_surface_v1_set_anchor(output->wlr_layer_surface, bar.anchor);
  zwlr_layer_surface_v1_set_size(output->wlr_layer_surface,
                                 initial_width, initial_height);
  zwlr_layer_surface_v1_set_margin(output->wlr_layer_surface, 0, 0, 0, 0);
  zwlr_layer_surface_v1_set_exclusive_zone(output->wlr_layer_surface, bar.size);

  request_frame(output);
}

static void register_output(struct wl_output* wl_output, u32 name) {
  struct output* output = calloc(sizeof(*output), 1);
  assert(output != NULL);

  /* Add the output to the linked list */
  if (wl.outputs == NULL)
    wl.outputs = output;
  else {
    wl.outputs->prev = output;
    output->next = wl.outputs;
    wl.outputs = output;
  }

  output->wl_output = wl_output;
  output->id = name;

  if (wl.init_done)
    init_output(output);
}

static void wl_shm_handle_format(void* _, struct wl_shm* wl_shm, u32 format) {
  assert(wl_shm == wl.wl_shm);
  switch (format) {
    case WL_SHM_FORMAT_ARGB8888:
    case WL_SHM_FORMAT_XRGB8888:
      wl.output_format = format;
      break;
    default:
      break;
  }
}

static const struct wl_shm_listener wl_shm_listener = {
  .format = wl_shm_handle_format
};

static void wl_registry_handle_global(void* _, struct wl_registry* wl_registry,
                                      u32 name, const char* interface,
                                      u32 version) {
  struct wl_output* wl_output;

  assert(wl_registry == wl.wl_registry);
  log_trace("got interface %s (name: %u, version: %u)",
            interface, name, version);

  /* wl_compositor */
  if (!strcmp(interface, wl_compositor_interface.name))
    wl.wl_compositor = wl_registry_bind(wl_registry, name,
                                         &wl_compositor_interface, version);
  /* xdg_output_manager */
  else if (!strcmp(interface, zxdg_output_manager_v1_interface.name))
    wl.xdg_output_manager =
      wl_registry_bind(wl_registry, name, &zxdg_output_manager_v1_interface,
                       version);
  /* wlr_layer_shell */
  else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name))
    wl.wlr_layer_shell = wl_registry_bind(wl_registry, name,
                                           &zwlr_layer_shell_v1_interface,
                                           version);
  /* wl_shm */
  else if (!strcmp(interface, wl_shm_interface.name)) {
    wl.wl_shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, version);
    wl_shm_add_listener(wl.wl_shm, &wl_shm_listener, NULL);
  }
  /* wl_output */
  else if (!strcmp(interface, wl_output_interface.name)) {
    wl_output = wl_registry_bind(wl_registry, name, &wl_output_interface,
                                 version);
    register_output(wl_output, name);
  }
}

static void wl_registry_handle_global_remove(void* _,
                                             struct wl_registry* wl_registry,
                                             u32 name) {
  assert(wl_registry == wl.wl_registry);
  log_trace("got global remove event (name: %u)", name);

  /* Check if remove event refers to an output */
  FOREACHOUTPUT(output) {
    if (output->id == name) {
      remove_output(output);
      return;
    }
  }
}

static const struct wl_registry_listener wl_registry_listener = {
  .global = wl_registry_handle_global,
  .global_remove = wl_registry_handle_global_remove
};

static enum zwlr_layer_surface_v1_anchor
position_to_anchor(enum bar_position position) {
#define CASE(x) \
  case BAR_POSITION_##x: \
    return ZWLR_LAYER_SURFACE_V1_ANCHOR_##x

  switch (position) {
    CASE(BOTTOM);
    CASE(TOP);
    default:
      log_fatal("invalid bar position #%d", position);
  }

#undef CASE
}

static void int_handler(int signo) {
  assert(signo == SIGINT);
  should_close = true;
}

static void set_int_handler(void) {
  struct sigaction sigact;
  sigact.sa_flags = SA_RESTART;
  sigact.sa_handler = &int_handler;
  sigemptyset(&sigact.sa_mask);
  assert(sigaction(SIGINT, &sigact, NULL) == 0);
}

static void restore_int_handler(void) {
  struct sigaction sigact;
  sigact.sa_flags = 0;
  sigact.sa_handler = SIG_DFL;
  sigemptyset(&sigact.sa_mask);
  assert(sigaction(SIGINT, &sigact, NULL) == 0);
}

int wl_init(enum bar_position position, u32 size) {
  struct output *output, *next_output;

  set_int_handler();

  bar.anchor = position_to_anchor(position);
  bar.size = size;

  /* Set invalid output format */
  wl.output_format = -1;
  /* Reset post_init flag */
  wl.init_done = false;

  wl.wl_display = wl_display_connect(NULL);
  if (wl.wl_display == NULL) {
    log_error("could not connect to wayland display");
    return -1;
  }
  log_trace("connected to wayland display");

  wl.wl_registry = wl_display_get_registry(wl.wl_display);
  if (wl.wl_registry == NULL) {
    log_error("could not get display registry");
    return -1;
  }
  log_trace("got display registry");

  wl_registry_add_listener(wl.wl_registry, &wl_registry_listener, NULL);
  wl_display_roundtrip(wl.wl_display);

#define CHECK(x)                                                   \
  do {                                                             \
    if (wl.x == NULL) {                                            \
      log_error("could not get %s interface", x##_interface.name); \
      return -1;                                                   \
    }                                                              \
  } while (0)
#define CHECKUNSTABLE(x, v)                                                  \
  do {                                                                       \
    if (wl.x == NULL) {                                                      \
      log_error("could not get %s interface", z##x##_v##v##_interface.name); \
      return -1;                                                             \
    }                                                                        \
  } while (0)

  CHECK(wl_compositor);
  CHECK(wl_shm);
  CHECKUNSTABLE(wlr_layer_shell, 1);
  CHECKUNSTABLE(xdg_output_manager, 1);

  if (wl.outputs == NULL) {
    log_error("could not detect any screen");
    return -1;
  }
  log_trace("got all the wayland interfaces");

#undef CHECKUNSTABLE
#undef CHECK

  /* Find output format if we still don't have one */
  if (wl.output_format == (u32)-1) {
    wl_display_roundtrip(wl.wl_display);
    if (wl.output_format == (u32)-1) {
      log_error("could not find a valid shm format");
      return -1;
    }
  }

  /* Initialize all remaining non-initialized outputs */
  for (output = wl.outputs; output != NULL; output = next_output) {
    next_output = output->next;
    if (output->xdg_output == NULL)
      init_output(output);
  }

  wl.init_done = true;
  /* Do one more roundtrip to finish initialization */
  wl_display_roundtrip(wl.wl_display);

  wl.display_fd = wl_display_get_fd(wl.wl_display);

  return 0;
}

int wl_should_close(void) {
  int rc;
  struct pollfd pfd = {
    .fd = wl.display_fd,
    .events = POLLIN
  };

  /* Send all buffered requests to the compositor */
  wl_display_flush(wl.wl_display);

  /* Poll for events from the compositor */
  rc = poll(&pfd, 1, 0);
  /* Check for errors */
  should_close |= rc < 0 && errno != EINTR;
  should_close |= (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
  if (should_close)
    goto out;
  /* Check for events */
  else if (pfd.revents & POLLIN) {
    assert(wl_display_prepare_read(wl.wl_display) == 0);
    wl_display_read_events(wl.wl_display);

    /* Dispatch events */
    while (!should_close) {
      rc = wl_display_dispatch_pending(wl.wl_display);
      if (rc == 0)
        break;
      should_close |= rc < 0;
    }
  }

out:
  return should_close;
}

b8 wl_draw_begin(void) {
  FOREACHOUTPUT(output) {
    if (!output->frame_done)
      return false;
  }
  FOREACHOUTPUT(output) {
    if (output->wl_surface == NULL || output->wl_buffer == NULL) {
      log_warn("output %s (id: %u) has not been initialized",
               output_name(output), output->id);
      continue;
    }
    wl_surface_attach(output->wl_surface, output->wl_buffer, 0, 0);
  }
  return true;
}

void wl_draw_end(void) {
  FOREACHOUTPUT(output) {
    if (output->wl_surface != NULL)
      request_frame(output);
  }
}

void wl_cleanup(void) {
  struct output *output, *next_output;

  /* Destroy all outputs */
  for (output = wl.outputs; output != NULL; output = next_output) {
    next_output = output->next;
    remove_output(output);
  }
  wl.outputs = NULL;

#define DESTROY(x) \
  do { if (wl.x != NULL) x##_destroy(wl.x); } while (0)
#define DESTROYUNSTABLE(x, v) \
  do { if (wl.x != NULL) z##x##_v##v##_destroy(wl.x); } while (0)

  DESTROYUNSTABLE(wlr_layer_shell, 1);
  DESTROYUNSTABLE(xdg_output_manager, 1);
  DESTROY(wl_shm);
  DESTROY(wl_compositor);
  DESTROY(wl_registry);

#undef DESTROYUNSTABLE
#undef DESTROY

  if (wl.wl_display != NULL)
    wl_display_disconnect(wl.wl_display);

  restore_int_handler();
}

static void fill_buffer_region(u32 src_x, u32 src_y,
                               u32 dst_x, u32 dst_y,
                               u32 width, u32 height,
                               u32* src, u32 src_stride,
                               u32* dst, u32 dst_stride) {
  size_t i_src, i_dst;
  size_t w, h;

  i_src = src_y * src_stride + src_x;
  i_dst = dst_y * dst_stride + dst_x;

  /* NOTE: Drawing is done on the CPU, and is very slow.
   *       Call this function sparingly.
   */
  for (h = 0; h < height; ++h) {
    for (w = 0; w < width; ++w)
      dst[i_dst + w] = src[i_src + w];
    i_src += src_stride;
    i_dst += dst_stride;
  }
}

void wl_draw_element(f32 x, f32 y, u32 width, u32 height, void* data) {
  i32 output_x, output_y;
  u32 data_x, data_y;
  u32 start_x, start_y;
  u32 end_x, end_y;
  u32 draw_width, draw_height;

  if (x < -1.0f || x > 1.0f)
    log_warn("suspicious draw x value: %.2f "
             "(should between [-1.0f, +1.0f])", x);
  if (y < -1.0f || y > 1.0f)
    log_warn("suspicious draw y value: %.2f "
             "(should between [-1.0f, +1.0f])", y);

  /* Both x and y are between [-1.0f, +1.0f], so we convert them be
   * between [0.0f, 1.0f].
   */
  x = (x + 1.0f) * 0.5f;
  y = (y + 1.0f) * 0.5f;

  FOREACHOUTPUT(output) {
    if (output->buffer == NULL) {
      log_warn("output %s (id: %u) has buffer == NULL",
               output_name(output), output->id);
      continue;
    }

    output_x = output->surface_width * x - (width >> 1);
    data_x   = output_x < 0 ? -output_x : 0;
    start_x  = clamp(output_x, 0, output->surface_width);
    end_x    = clamp(output_x + width, 0, output->surface_width);
    assert(start_x <= end_x);

    output_y = output->surface_height * y - (height >> 1);
    data_y   = output_y < 0 ? -output_y : 0;
    start_y  = clamp(output_y, 0, output->surface_height);
    end_y    = clamp(output_y + height, 0, output->surface_height);
    assert(start_y <= end_y);

    draw_width  = end_x - start_x;
    draw_height = end_y - start_y;

    /* Nothing to draw, continue to the next monitor */
    if (draw_height == 0 || draw_width == 0) {
      log_warn("nothing to draw on output %s (id: %u)",
               output_name(output), output->id);
      continue;
    }

    fill_buffer_region(data_x, data_y, start_x, start_y,
                       draw_width, draw_height,
                       data, width,
                       output->buffer, output->surface_width);

    wl_surface_damage_buffer(output->wl_surface,
                             start_x, start_y,
                             draw_width, draw_height);
  }
}

void wl_clear(u32 color) {
  FOREACHOUTPUT(output) {
    if (output->buffer == NULL) {
      log_warn("output %s (id: %u) has buffer == NULL",
               output_name(output), output->id);
      continue;
    }
    /* Use wmemset(..) for semplicity */
    wmemset((int*)output->buffer, color, output->buffer_size >> 2);
    wl_surface_damage_buffer(output->wl_surface, 0, 0, output->surface_width, output->surface_height);
  }
}
