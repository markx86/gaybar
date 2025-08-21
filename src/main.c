#include <gaybar/params.h>
#include <gaybar/bar.h>
#include <gaybar/log.h>
#include <gaybar/config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#define eprintf(x, ...) fprintf(stderr, x, ##__VA_ARGS__)
#define eputs(x) fputs(x "\n", stderr)

struct params g_params = {0};

static void usage(const char* argv0) {
  eprintf("USAGE: %s [OPTIONS]\n", argv0);
  eputs("");
  eputs("Supported options");
  eputs(" -h          Print help and exit");
  eputs(" -L LEVEL    Set log level to LEVEL");
  eputs(" -f FILE     Set log file path to FILE");
  eputs(" -c FILE     Load configuration from FILE");
}

static int params_parse(int argc, char* argv[]) {
  int opt;
  char* endptr;

  while ((opt = getopt(argc, argv, "hL:f:c:")) != -1) {
    switch (opt) {
      case 'h':
        usage(argv[0]);
        exit(EXIT_SUCCESS);
      case 'L':
        g_params.log_level = strtol(optarg, &endptr, 10);
        if (endptr == NULL || *endptr != '\0')
          g_params.log_level = LOG_LEVEL_MIN;
        break;
      case 'f':
        g_params.log_file = strdup(optarg);
        break;
      case 'c':
        g_params.config_file = strdup(optarg);
        break;
      case '?':
        if (isprint(optopt))
          eprintf("Unknown option '-%c'\n", optopt);
        else
          eprintf("Invalid option character %#04x", optopt);
        usage(argv[0]);
        return -1;
      default:
        abort();
    }
  }

  return 0;
}

static void params_free(void) {
  if (g_params.log_file)
    free(g_params.log_file);
  if (g_params.config_file)
    free(g_params.config_file);
}

int main(int argc, char* argv[]) {
  int rc;

  rc = params_parse(argc, argv);
  if (rc < 0)
    goto args_fail;

  log_init();
  config_load();

  rc = bar_init();
  if (rc < 0)
    goto bar_fail;

  bar_loop();
  rc = 0;
bar_fail:
  bar_cleanup();
  config_unload();
  log_cleanup();
  params_free();
args_fail:
  return rc;
}
