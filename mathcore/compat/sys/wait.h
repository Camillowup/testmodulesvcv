/* Windows stand-in for <sys/wait.h>: the firmware's host build (jiant/tests/hostsim.c) includes it for its
 * test harness, which the VCV core never calls. These stubs only let it compile. */
#ifndef JIANT_COMPAT_SYS_WAIT_H
#define JIANT_COMPAT_SYS_WAIT_H
#include <sys/types.h>
#include <unistd.h>
#ifndef WIFEXITED
#define WIFEXITED(s) 1
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(s) 0
#endif
static inline int fork(void) { return -1; }
static inline int pipe(int *fd) { (void)fd; return -1; }
static inline int waitpid(int pid, int *st, int opt) { (void)pid; (void)st; (void)opt; return -1; }
#endif
