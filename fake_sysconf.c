#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#ifndef FAKE_NPROCESSORS
#error "FAKE_NPROCESSORS must be defined at compile time"
#endif

#ifdef FAKE_NPROCESSORS
typedef long (*real_sysconf_t)(int);
typedef int (*real_sched_getaffinity_t)(pid_t pid, size_t cpusetsize,
                                        cpu_set_t *mask);
typedef int (*real_pthread_getaffinity_np_t)(pthread_t thread,
                                             size_t cpusetsize,
                                             cpu_set_t *cpuset);

long sysconf(int name) {
  real_sysconf_t real_sysconf = dlsym(RTLD_NEXT, "sysconf");

  if (name == _SC_NPROCESSORS_ONLN || name == _SC_NPROCESSORS_CONF) {
    return FAKE_NPROCESSORS;
  }

  return real_sysconf(name);
}

int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
  real_sched_getaffinity_t real_sched_getaffinity =
      dlsym(RTLD_NEXT, "sched_getaffinity");

  int result = real_sched_getaffinity(pid, cpusetsize, mask);
  if (result == 0) {
    CPU_ZERO(mask);
    for (int i = 0; i < FAKE_NPROCESSORS && i < CPU_SETSIZE; i++) {
      CPU_SET(i, mask);
    }
  }
  return result;
}

int pthread_getaffinity_np(pthread_t thread, size_t cpusetsize,
                           cpu_set_t *cpuset) {
  real_pthread_getaffinity_np_t real_pthread_getaffinity_np =
      dlsym(RTLD_NEXT, "pthread_getaffinity_np");

  int result = real_pthread_getaffinity_np(thread, cpusetsize, cpuset);
  if (result == 0) {
    CPU_ZERO(cpuset);
    for (int i = 0; i < FAKE_NPROCESSORS && i < CPU_SETSIZE; i++) {
      CPU_SET(i, cpuset);
    }
  }
  return result;
}
#endif
