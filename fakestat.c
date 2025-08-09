/**
 * @file fakestat.c
 * @brief LD_PRELOAD library that fakes /proc/stat by intercepting file operations
 *
 * This library creates a scaled/duplicated copy of /proc/stat at TMP_STAT_PATH
 * and transparently redirects open/fopen family calls to that file when the target
 * is /proc/stat. It duplicates CPU core entries to match the fake CPU count.
 *
 * @section build Build
 * @code
 * clang -shared -fPIC -o fakestat.so fakestat.c -ldl
 * @endcode
 *
 * @section environment Environment Variables
 * @par FAKESTAT_CORES
 * Target number of CPU cores to show in /proc/stat. If not set, defaults to
 * the actual number of cores (no faking).
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_STAT_SIZE 32768
#define TMP_STAT_PATH "/tmp/stat_scaled"

static int target_cores = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int watcher_thread_started = 0;
static pthread_t watcher_thread;

/**
 * @brief Gets the number of CPU cores by running a shell command
 * @return Number of CPU cores detected, or 0 on error
 */
static int get_core_count_from_sysfs() {
  FILE *pipe = popen("ls /sys/devices/system/cpu/ | grep -E 'cpu[0-9]+' | wc -l", "r");
  if (!pipe)
    return 0;

  char buffer[32];
  if (fgets(buffer, sizeof(buffer), pipe) == NULL) {
    pclose(pipe);
    return 0;
  }

  pclose(pipe);
  return atoi(buffer);
}

/**
 * @brief Creates a scaled version of /proc/stat with duplicated CPU entries
 *
 * Reads the real /proc/stat, duplicates CPU core entries to match FAKESTAT_CORES,
 * and writes the result to TMP_STAT_PATH.
 */
static void create_fake_stat_file() {
  static FILE *(*real_fopen)(const char *, const char *) = NULL;
  if (!real_fopen)
    real_fopen = dlsym(RTLD_NEXT, "fopen");

  FILE *real_file = real_fopen("/proc/stat", "r");
  if (!real_file)
    return;

  char buffer[MAX_STAT_SIZE];
  size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, real_file);
  fclose(real_file);
  if (bytes_read == 0)
    return;
  buffer[bytes_read] = '\0';

  target_cores = get_core_count_from_sysfs();
  if (target_cores <= 0)
    return;

  char scaled_stat[MAX_STAT_SIZE * 4] = {0};
  char *pos = scaled_stat;
  size_t remaining = sizeof(scaled_stat);

  char cpu_lines[256][512];
  int real_cores = 0;

  char *saveptr, *line = strtok_r(buffer, "\n", &saveptr);
  char global_cpu_line[512] = {0};

  while (line) {
    if (strncmp(line, "cpu ", 4) == 0) {
      strncpy(global_cpu_line, line, sizeof(global_cpu_line) - 1);
      break;
    }
    line = strtok_r(NULL, "\n", &saveptr);
  }

  line = strtok_r(NULL, "\n", &saveptr);
  while (line && strncmp(line, "cpu", 3) == 0) {
    if (sscanf(line, "cpu%*d") == 0 && real_cores < 256) {
      strncpy(cpu_lines[real_cores], line, sizeof(cpu_lines[real_cores]) - 1);
      cpu_lines[real_cores][sizeof(cpu_lines[real_cores]) - 1] = '\0';
      real_cores++;
    }
    line = strtok_r(NULL, "\n", &saveptr);
  }

  unsigned long long total_user = 0, total_nice = 0, total_system = 0, total_idle = 0;
  unsigned long long total_iowait = 0, total_irq = 0, total_softirq = 0, total_steal = 0;
  unsigned long long total_guest = 0, total_guest_nice = 0;

  for (int i = 0; i < target_cores && remaining > 1; i++) {
    char fake_line[512];
    const char *real_line = cpu_lines[i % real_cores];

    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    if (sscanf(real_line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle,
               &iowait, &irq, &softirq, &steal, &guest, &guest_nice) >= 4) {

      int static_offset = (i * 37) % 100;
      unsigned long long var_user = user + static_offset;
      unsigned long long var_system = system + (static_offset / 3);
      unsigned long long var_idle = idle + (static_offset * 2);

      snprintf(fake_line, sizeof(fake_line), "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", i, var_user,
               nice, var_system, var_idle, iowait, irq, softirq, steal, guest, guest_nice);

      total_user += var_user;
      total_nice += nice;
      total_system += var_system;
      total_idle += var_idle;
      total_iowait += iowait;
      total_irq += irq;
      total_softirq += softirq;
      total_steal += steal;
      total_guest += guest;
      total_guest_nice += guest_nice;
    } else {
      char *space_pos = strchr(real_line, ' ');
      if (space_pos) {
        snprintf(fake_line, sizeof(fake_line), "cpu%d%s", i, space_pos);
      } else {
        snprintf(fake_line, sizeof(fake_line), "cpu%d 0 0 0 0 0 0 0 0 0 0", i);
      }
    }

    int written = snprintf(pos, remaining, "%s\n", fake_line);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }

  char scaled_global_cpu[512];
  snprintf(scaled_global_cpu, sizeof(scaled_global_cpu), "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
           total_user, total_nice, total_system, total_idle, total_iowait, total_irq, total_softirq, total_steal,
           total_guest, total_guest_nice);

  memmove(scaled_stat + strlen(scaled_global_cpu) + 1, scaled_stat, strlen(scaled_stat) + 1);
  memcpy(scaled_stat, scaled_global_cpu, strlen(scaled_global_cpu));
  scaled_stat[strlen(scaled_global_cpu)] = '\n';

  pos = scaled_stat + strlen(scaled_stat);
  remaining = sizeof(scaled_stat) - strlen(scaled_stat);

  while (line && remaining > 1) {
    if (strncmp(line, "intr ", 5) == 0) {
      unsigned long long base_intr;
      if (sscanf(line, "intr %llu", &base_intr) == 1) {
        unsigned long long scaled_intr = base_intr * target_cores / real_cores;
        char *rest = strchr(line, ' ');
        rest = strchr(rest + 1, ' ');
        int written = snprintf(pos, remaining, "intr %llu%s\n", scaled_intr, rest ? rest : "");
        if (written > 0 && (size_t)written < remaining) {
          pos += written;
          remaining -= written;
        }
      } else {
        int written = snprintf(pos, remaining, "%s\n", line);
        if (written > 0 && (size_t)written < remaining) {
          pos += written;
          remaining -= written;
        }
      }
    } else if (strncmp(line, "ctxt ", 5) == 0) {
      unsigned long long base_ctxt;
      if (sscanf(line, "ctxt %llu", &base_ctxt) == 1) {
        unsigned long long scaled_ctxt = base_ctxt * target_cores / real_cores;
        int written = snprintf(pos, remaining, "ctxt %llu\n", scaled_ctxt);
        if (written > 0 && (size_t)written < remaining) {
          pos += written;
          remaining -= written;
        }
      } else {
        int written = snprintf(pos, remaining, "%s\n", line);
        if (written > 0 && (size_t)written < remaining) {
          pos += written;
          remaining -= written;
        }
      }
    } else {
      int written = snprintf(pos, remaining, "%s\n", line);
      if (written > 0 && (size_t)written < remaining) {
        pos += written;
        remaining -= written;
      }
    }
    line = strtok_r(NULL, "\n", &saveptr);
  }

  FILE *tmp_file = real_fopen(TMP_STAT_PATH, "w");
  if (tmp_file) {
    fwrite(scaled_stat, 1, strlen(scaled_stat), tmp_file);
    fclose(tmp_file);
  }
}

/**
 * @brief Background thread function that continuously updates /proc/stat
 * @param arg Unused thread argument
 * @return NULL
 *
 * Periodically regenerates the scaled copy of /proc/stat every 50ms
 * to ensure monitoring tools get fresh CPU usage data.
 */
static void *watcher_func(void *arg) {
  (void)arg;

  while (1) {
    pthread_mutex_lock(&mutex);
    create_fake_stat_file();
    pthread_mutex_unlock(&mutex);

    usleep(50000);
  }
  return NULL;
}

/**
 * @brief Redirects /proc/stat requests to the scaled copy
 * @param pathname Original file path
 * @return TMP_STAT_PATH if pathname is "/proc/stat", otherwise pathname unchanged
 *
 * Lazily spawns the watcher thread on first access and ensures the temp file exists.
 */
static const char *redirect_stat_path(const char *pathname) {
  if (strcmp(pathname, "/proc/stat") == 0) {
    pthread_mutex_lock(&mutex);
    if (!watcher_thread_started) {
      create_fake_stat_file();
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      pthread_create(&watcher_thread, &attr, watcher_func, NULL);
      pthread_attr_destroy(&attr);
      watcher_thread_started = 1;
    }
    pthread_mutex_unlock(&mutex);
    return TMP_STAT_PATH;
  }
  return pathname;
}

#define GET_REAL(func, return_type, args)                                                                              \
  static return_type(*real_##func) args = NULL;                                                                        \
  if (!real_##func)                                                                                                    \
    real_##func = dlsym(RTLD_NEXT, #func);

/**
 * @brief Interposed open(2) system call
 */
int open(const char *pathname, int flags, ...) {
  GET_REAL(open, int, (const char *, int, ...));
  pathname = redirect_stat_path(pathname);
  va_list args;
  va_start(args, flags);
  mode_t mode = va_arg(args, mode_t);
  va_end(args);
  return real_open(pathname, flags, mode);
}

/**
 * @brief Interposed open64(2) system call
 */
int open64(const char *pathname, int flags, ...) {
  GET_REAL(open64, int, (const char *, int, ...));
  pathname = redirect_stat_path(pathname);
  va_list args;
  va_start(args, flags);
  mode_t mode = va_arg(args, mode_t);
  int result = real_open64(pathname, flags, mode);
  va_end(args);
  return result;
}

/**
 * @brief Interposed openat(2) system call
 */
int openat(int dirfd, const char *pathname, int flags, ...) {
  GET_REAL(openat, int, (int, const char *, int, ...));
  pathname = redirect_stat_path(pathname);
  va_list args;
  va_start(args, flags);
  mode_t mode = va_arg(args, mode_t);
  int result = real_openat(dirfd, pathname, flags, mode);
  va_end(args);
  return result;
}

/**
 * @brief Interposed fopen(3) library function
 */
FILE *fopen(const char *pathname, const char *mode) {
  GET_REAL(fopen, FILE *, (const char *, const char *));
  pathname = redirect_stat_path(pathname);
  return real_fopen(pathname, mode);
}

/**
 * @brief Interposed fopen64(3) library function
 */
FILE *fopen64(const char *pathname, const char *mode) {
  GET_REAL(fopen64, FILE *, (const char *, const char *));
  pathname = redirect_stat_path(pathname);
  return real_fopen64(pathname, mode);
}

/**
 * @brief Interposed freopen(3) library function
 */
FILE *freopen(const char *pathname, const char *mode, FILE *stream) {
  GET_REAL(freopen, FILE *, (const char *, const char *, FILE *));
  pathname = redirect_stat_path(pathname);
  return real_freopen(pathname, mode, stream);
}

/**
 * @brief Interposed freopen64(3) library function
 */
FILE *freopen64(const char *pathname, const char *mode, FILE *stream) {
  GET_REAL(freopen64, FILE *, (const char *, const char *, FILE *));
  pathname = redirect_stat_path(pathname);
  return real_freopen64(pathname, mode, stream);
}