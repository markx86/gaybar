#include <gaybar/wl.h>
#include <gaybar/bar.h>
#include <gaybar/log.h>
#include <gaybar/util.h>
#include <gaybar/list.h>
#include <gaybar/assert.h>
#include <gaybar/compiler.h>

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

STATIC_ASSERT(sizeof(wchar_t) == sizeof(u32));

struct output {
  struct list link;
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
  b8 frame_done, buffer_dirty;
};

struct wl {
  enum zwlr_layer_surface_v1_anchor anchor;
  struct wl_display* wl_display;
  struct wl_registry* wl_registry;
  struct wl_compositor* wl_compositor;
  struct wl_shm* wl_shm;
  struct zxdg_output_manager_v1* xdg_output_manager;
  struct zwlr_layer_shell_v1* wlr_layer_shell;
  struct list outputs;
  u32 output_format;
  b8 init_done, can_draw;
};

struct wl g_wl = {0};
static b8 g_should_close = false;

static void request_frame(struct output* output);

static inline char* output_name(struct output* output) {
  return output->name == NULL ? "UNK" : output->name;
}

static void wl_buffer_handle_release(void* data, struct wl_buffer* wl_buffer) {
  struct output* output = data;
  ASSERT(wl_buffer == output->wl_buffer);
  log_trace("buffer released");
}

static const struct wl_buffer_listener g_wl_buffer_listener = {
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

  wl_shm_pool = wl_shm_create_pool(g_wl.wl_shm, fd, size);
  output->wl_buffer = wl_shm_pool_create_buffer(wl_shm_pool, 0,
                                                output->surface_width,
                                                output->surface_height,
                                                stride, g_wl.output_format);
  wl_shm_pool_destroy(wl_shm_pool);

  wl_buffer_add_listener(output->wl_buffer, &g_wl_buffer_listener, output);
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
    wl_display_roundtrip(g_wl.wl_display);
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

static void free_output(struct output* output) {
  if (output->name != NULL)
    free(output->name);
  if (output->description != NULL)
    free(output->description);
  free(output);
}

static void remove_output(struct output* output) {
  ASSERT(output != NULL);
  log_trace("removing output %s", output_name(output));
  /* Remove the output from the linked list */
  list_remove(&output->link);
  /* Free wayland objects */
#define DESTROY(x) \
  do { if (output->x != NULL) x##_destroy(output->x); } while (0)
#define DESTROYUNSTABLE(x, v) \
  do { if (output->x != NULL) z##x##_v##v##_destroy(output->x); } while (0)

  DESTROY(wl_callback);
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
  ASSERT(xdg_output == output->xdg_output);
  output->name = strdup(name);
}

static void xdg_output_handle_description(void* data,
                                          struct zxdg_output_v1* xdg_output,
                                          const char* description) {
  struct output* output = data;
  ASSERT(xdg_output == output->xdg_output);
  output->description = strdup(description);
}

static void
xdg_output_handle_logical_position(void* data,
                                   struct zxdg_output_v1* xdg_output,
                                   i32 x, i32 y) {
  struct output* output = data;
  ASSERT(xdg_output == output->xdg_output);
  output->x = x;
  output->y = y;
}

static void
xdg_output_handle_logical_size(void* data,
                               struct zxdg_output_v1* xdg_output,
                               i32 width, i32 height) {
  struct output* output = data;
  ASSERT(xdg_output == output->xdg_output);
  log_trace("got xdg output logical size (width = %d, height = %d)",
            width, height);
  output->width = width;
  output->height = height;
}

static void xdg_output_handle_done(void* data,
                                   struct zxdg_output_v1* xdg_output) {
  struct output* output = data;
  ASSERT(xdg_output == output->xdg_output);
  log_trace("got done event for output %s", output_name(output));
}

static const struct zxdg_output_v1_listener g_xdg_output_listener = {
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
  ASSERT(wlr_layer_surface == output->wlr_layer_surface);
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
  ASSERT(wlr_layer_surface == output->wlr_layer_surface);
  g_should_close = true;
}

static const struct zwlr_layer_surface_v1_listener g_wlr_layer_surface_listener=
{
  .closed = wlr_layer_surface_handle_closed,
  .configure = wlr_layer_surface_handle_configure
};

static void wl_callback_frame_handle_done(void* data,
                                          struct wl_callback* wl_callback,
                                          u32 timestamp) {
  struct output* output = data;
  ASSERT(wl_callback == output->wl_callback);
  UNUSED(timestamp);
  /* Clear the callback */
  wl_callback_destroy(wl_callback);
  output->wl_callback = NULL;
  /* Signal that this output is done with its frame */
  output->frame_done = true;
}

static const struct wl_callback_listener g_wl_callback_frame_listener = {
  .done = wl_callback_frame_handle_done
};

static void request_frame(struct output* output) {
  ASSERT(output->wl_callback == NULL);
  /* Create the frame callback for the surface */
  output->wl_callback = wl_surface_frame(output->wl_surface);
  wl_callback_add_listener(output->wl_callback, &g_wl_callback_frame_listener,
                           output);
  wl_surface_commit(output->wl_surface);
  /* When a frame has been requested, we can't draw */
  output->frame_done = false;
}

static void init_output(struct output* output) {
  u32 initial_width, initial_height;
  u32 bar_thickness = bar_get_thickness();

  /* Create xdg output */
  output->xdg_output =
    zxdg_output_manager_v1_get_xdg_output(g_wl.xdg_output_manager,
                                          output->wl_output);
  zxdg_output_v1_add_listener(output->xdg_output, &g_xdg_output_listener,
                              output);

  /* Roundtrip to get output witdth and output height */
  wl_display_roundtrip(g_wl.wl_display);
  if (output->width == 0 || output->height == 0) {
    remove_output(output);
    return;
  }

  /* Compute bar size
   *
   * NOTE: Here we are assuming the bar is in an horizontal position
   *       (i.e. anchored to the top or bottom of the screen). If we ever want
   *       to support vertical bars (anchored to the left or right of the
   *       screen), we will need to check whether the bar is vertical or
   *       horizonal and compute the surface width and height accordingly.
   */
  {
    ASSERT(bar_thickness < output->height);
    initial_width = output->width;
    initial_height = bar_thickness;
  }

  /* Create surface */
  output->wl_surface = wl_compositor_create_surface(g_wl.wl_compositor);

  /* Create wlr layer surface */
  output->wlr_layer_surface =
    zwlr_layer_shell_v1_get_layer_surface(g_wl.wlr_layer_shell,
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
                                     &g_wlr_layer_surface_listener,
                                     output);

  /* Set wlr layer surface position, anchor, margin and exclusive zone */
  zwlr_layer_surface_v1_set_anchor(output->wlr_layer_surface, g_wl.anchor);
  zwlr_layer_surface_v1_set_size(output->wlr_layer_surface,
                                 initial_width, initial_height);
  zwlr_layer_surface_v1_set_margin(output->wlr_layer_surface, 0, 0, 0, 0);
  zwlr_layer_surface_v1_set_exclusive_zone(output->wlr_layer_surface,
                                           bar_thickness);

  request_frame(output);
}

static void register_output(struct wl_output* wl_output, u32 name) {
  struct output* output = zalloc(sizeof(*output));
  ASSERT(output != NULL);

  /* Add the output to the linked list */
  list_insert(&g_wl.outputs, &output->link);

  output->wl_output = wl_output;
  output->id = name;

  if (g_wl.init_done)
    init_output(output);
}

static void wl_shm_handle_format(void* _, struct wl_shm* wl_shm, u32 format) {
  ASSERT(wl_shm == g_wl.wl_shm);
  switch (format) {
    case WL_SHM_FORMAT_ARGB8888:
    case WL_SHM_FORMAT_XRGB8888:
      g_wl.output_format = format;
      break;
    default:
      break;
  }
}

static const struct wl_shm_listener g_wl_shm_listener = {
  .format = wl_shm_handle_format
};

static void wl_registry_handle_global(void* _, struct wl_registry* wl_registry,
                                      u32 name, const char* interface,
                                      u32 version) {
  struct wl_output* wl_output;

  ASSERT(wl_registry == g_wl.wl_registry);
  log_trace("got interface %s (name: %u, version: %u)",
            interface, name, version);

  /* wl_compositor */
  if (!strcmp(interface, wl_compositor_interface.name))
    g_wl.wl_compositor =
      wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
  /* xdg_output_manager */
  else if (!strcmp(interface, zxdg_output_manager_v1_interface.name))
    g_wl.xdg_output_manager =
      wl_registry_bind(wl_registry, name, &zxdg_output_manager_v1_interface,
                       version);
  /* wlr_layer_shell */
  else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name))
    g_wl.wlr_layer_shell =
      wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface,
                       version);
  /* wl_shm */
  else if (!strcmp(interface, wl_shm_interface.name)) {
    g_wl.wl_shm =
      wl_registry_bind(wl_registry, name, &wl_shm_interface, version);
    wl_shm_add_listener(g_wl.wl_shm, &g_wl_shm_listener, NULL);
  }
  /* wl_output */
  else if (!strcmp(interface, wl_output_interface.name)) {
    wl_output =
      wl_registry_bind(wl_registry, name, &wl_output_interface, version);
    register_output(wl_output, name);
  }
}

static void wl_registry_handle_global_remove(void* _,
                                             struct wl_registry* wl_registry,
                                             u32 name) {
  struct output* output;

  ASSERT(wl_registry == g_wl.wl_registry);
  log_trace("got global remove event (name: %u)", name);

  /* Check if remove event refers to an output */
  list_for_each(output, &g_wl.outputs, link) {
    if (output->id == name) {
      remove_output(output);
      return;
    }
  }
}

static const struct wl_registry_listener g_wl_registry_listener = {
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
  ASSERT(signo == SIGINT);
  g_should_close = true;
}

static void set_int_handler(void) {
  struct sigaction sigact;
  sigact.sa_flags = SA_RESTART;
  sigact.sa_handler = &int_handler;
  sigemptyset(&sigact.sa_mask);
  ASSERT(sigaction(SIGINT, &sigact, NULL) == 0);
}

static void restore_int_handler(void) {
  struct sigaction sigact;
  sigact.sa_flags = 0;
  sigact.sa_handler = SIG_DFL;
  sigemptyset(&sigact.sa_mask);
  ASSERT(sigaction(SIGINT, &sigact, NULL) == 0);
}

int wl_init(void) {
  struct output *output, *next_output;

  set_int_handler();

  g_wl.anchor = position_to_anchor(bar_get_position());

  /* Set invalid output format */
  g_wl.output_format = -1;
  /* Initialize output list */
  list_init(&g_wl.outputs);

  g_wl.wl_display = wl_display_connect(NULL);
  if (g_wl.wl_display == NULL) {
    log_error("could not connect to wayland display");
    return -1;
  }
  log_trace("connected to wayland display");

  g_wl.wl_registry = wl_display_get_registry(g_wl.wl_display);
  if (g_wl.wl_registry == NULL) {
    log_error("could not get display registry");
    return -1;
  }
  log_trace("got display registry");

  wl_registry_add_listener(g_wl.wl_registry, &g_wl_registry_listener, NULL);
  wl_display_roundtrip(g_wl.wl_display);

#define CHECK(x)                                                   \
  do {                                                             \
    if (g_wl.x == NULL) {                                          \
      log_error("could not get %s interface", x##_interface.name); \
      return -1;                                                   \
    }                                                              \
  } while (0)
#define CHECKUNSTABLE(x, v)                                                  \
  do {                                                                       \
    if (g_wl.x == NULL) {                                                    \
      log_error("could not get %s interface", z##x##_v##v##_interface.name); \
      return -1;                                                             \
    }                                                                        \
  } while (0)

  CHECK(wl_compositor);
  CHECK(wl_shm);
  CHECKUNSTABLE(wlr_layer_shell, 1);
  CHECKUNSTABLE(xdg_output_manager, 1);

  if (list_empty(&g_wl.outputs)) {
    log_error("could not detect any screen");
    return -1;
  }
  log_trace("got all the wayland interfaces");

#undef CHECKUNSTABLE
#undef CHECK

  /* Find output format if we still don't have one */
  if (g_wl.output_format == (u32)-1) {
    wl_display_roundtrip(g_wl.wl_display);
    if (g_wl.output_format == (u32)-1) {
      log_error("could not find a valid shm format");
      return -1;
    }
  }

  /* Initialize all remaining non-initialized outputs */
  list_for_each_safe(output, next_output, &g_wl.outputs, link) {
    if (output->xdg_output == NULL) {
      init_output(output);
      log_trace("initialized output %s (id: %u)",
                output_name(output), output->id);
    }
  }

  g_wl.init_done = true;
  /* Do one more roundtrip to finish initialization */
  wl_display_roundtrip(g_wl.wl_display);

  return 0;
}

int wl_should_close(void) {
  int rc;
  struct pollfd pfd = {
    .fd = wl_display_get_fd(g_wl.wl_display),
    .events = POLLIN
  };

  /* NOTE: This is a very complicated loop to achieve what
   *       wl_display_dispatch(..) achieves. We do this because
   *       wl_display_dispatch(..) does not return if the poll syscall was
   *       interrupted, and we need it to return to run the scheduler.
   */

  /* Send all buffered requests to the compositor */
  wl_display_flush(g_wl.wl_display);

  /* Poll for events from the compositor */
  rc = poll(&pfd, 1, -1);
  /* Check for errors */
  g_should_close |= rc < 0 && errno != EINTR;
  g_should_close |= (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
  if (g_should_close)
    goto out;
  /* Check for events */
  else if (pfd.revents & POLLIN) {
    ASSERT(wl_display_prepare_read(g_wl.wl_display) == 0);
    wl_display_read_events(g_wl.wl_display);

    /* Dispatch events */
    while (!g_should_close) {
      rc = wl_display_dispatch_pending(g_wl.wl_display);
      if (rc == 0)
        break;
      g_should_close |= rc < 0;
    }
  }

out:
  return g_should_close;
}

b8 wl_draw_begin(void) {
  struct output* output;
  list_for_each(output, &g_wl.outputs, link) {
    if (!output->frame_done)
      return false;
  }
  list_for_each(output, &g_wl.outputs, link) {
    if (output->wl_surface == NULL || output->wl_buffer == NULL) {
      log_warn("output %s (id: %u) has not been initialized",
               output_name(output), output->id);
      continue;
    }
    wl_surface_attach(output->wl_surface, output->wl_buffer, 0, 0);
    output->buffer_dirty = false;
  }
  return true;
}

void wl_draw_end(void) {
  struct output* output;
  list_for_each(output, &g_wl.outputs, link) {
    if (output->wl_surface != NULL && output->buffer_dirty)
      request_frame(output);
  }
}

void wl_cleanup(void) {
  struct output *output, *next_output;

  /* Destroy all outputs */
  list_for_each_safe(output, next_output, &g_wl.outputs, link)
    remove_output(output);

#define DESTROY(x) \
  do { if (g_wl.x != NULL) x##_destroy(g_wl.x); } while (0)
#define DESTROYUNSTABLE(x, v) \
  do { if (g_wl.x != NULL) z##x##_v##v##_destroy(g_wl.x); } while (0)

  DESTROYUNSTABLE(wlr_layer_shell, 1);
  DESTROYUNSTABLE(xdg_output_manager, 1);
  DESTROY(wl_shm);
  DESTROY(wl_compositor);
  DESTROY(wl_registry);

#undef DESTROYUNSTABLE
#undef DESTROY

  if (g_wl.wl_display != NULL)
    wl_display_disconnect(g_wl.wl_display);

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

static inline i32 get_offset(struct output* output, struct zone* zone,
                             u32 offset, u32 position_width) {
  switch (zone->position) {
    case ZONE_POSITION_LEFT:
      return offset;
    case ZONE_POSITION_CENTER:
      return ((output->surface_width - position_width) >> 1) + offset;
    case ZONE_POSITION_RIGHT:
      return output->surface_width - offset - zone->width;
    default:
      log_fatal("invalid zone position %d", zone->position);
  }
}

void wl_draw_zone(struct zone* zone, u32 offset, u32 position_width) {
  struct output* output;
  i32 start_x, end_x;
  const i32 start_y = 0, end_y = zone->height;

  list_for_each(output, &g_wl.outputs, link) {
    if (output->buffer == NULL) {
      log_warn("output %s (id: %u) has buffer == NULL",
               output_name(output), output->id);
      continue;
    }

    /* NOTE: The following code only works for horizontal bars.
     *       If we want to support vertical bars, we're going to have to
     *       adapt this.
     */

    start_x = get_offset(output, zone, offset, position_width);
    ASSERT(start_x >= 0);
    ASSERT(start_x <= (i32)(output->surface_width - zone->width));

    end_x = start_x + zone->width;
    ASSERT(end_x >= (i32)zone->width);
    ASSERT(end_x <= (i32)output->surface_width);

    ASSERT(end_x > start_x);
    ASSERT(end_y > start_y);

    ASSERT((u32)(end_x - start_x) == zone->width);
    ASSERT((u32)(end_y - start_y) == zone->height);

    fill_buffer_region(0, 0, start_x, start_y,
                       zone->width, zone->height,
                       zone->image_buffer, zone->width,
                       output->buffer, output->surface_width);

    wl_surface_damage_buffer(output->wl_surface,
                             start_x, start_y,
                             zone->width, zone->height);

    /* Mark the buffer as dirty */
    output->buffer_dirty = true;
  }
}

void wl_clear(u32 color) {
  struct output* output;
  list_for_each(output, &g_wl.outputs, link) {
    if (output->buffer == NULL) {
      log_warn("output %s (id: %u) has buffer == NULL",
               output_name(output), output->id);
      continue;
    }
    /* Use wmemset(..) for semplicity */
    wmemset((int*)output->buffer, color, output->buffer_size >> 2);
    wl_surface_damage_buffer(output->wl_surface, 0, 0,
                             output->surface_width, output->surface_height);
    /* Mark the buffer as dirty */
    output->buffer_dirty = true;
  }
}
