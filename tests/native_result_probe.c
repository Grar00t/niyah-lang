#define _GNU_SOURCE
/* Test-only observation of the full exit argument of our own child.
 * This does not instrument generated instructions or attach to existing
 * processes. Unsupported tracing is a test failure, never an 8-bit fallback.
 * Run under the suite's timeout; EXITKILL and PDEATHSIG protect child cleanup.
 */
#include <stdio.h>

#if !defined(__linux__) || !defined(__x86_64__)
int main(void) {
    fputs("native_result_probe: Linux x86-64 is required\n", stderr);
    return 1;
}
#else
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/ptrace.h>
#include <linux/ptrace.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t wait_child(pid_t child, int *status) {
    pid_t got;
    do { got = waitpid(child, status, 0); } while (got < 0 && errno == EINTR);
    return got;
}
static void reap_failed_child(pid_t child) {
    int status;
    /* The caller invokes this only while the unreaped child is still owned.
     * PTRACE_KILL alone is unreliable outside a signal-delivery stop. */
    if (kill(child, SIGKILL) < 0 && errno != ESRCH)
        perror("native_result_probe: kill child");
    if (wait_child(child, &status) < 0 && errno != ECHILD)
        perror("native_result_probe: reap child");
}
static int trace_child(pid_t child, uint64_t *full_result) {
    int status, alive = 1, exec_seen = 0, exit_seen = 0;
    uint64_t exit_argument = 0;
    unsigned long options = PTRACE_O_TRACEEXEC | PTRACE_O_TRACESYSGOOD |
                            PTRACE_O_EXITKILL;
    if (wait_child(child, &status) < 0) {
        int saved = errno;
        perror("native_result_probe: initial wait");
        if (saved == ECHILD) alive = 0;
        goto failed;
    }
    if (WIFEXITED(status) || WIFSIGNALED(status)) alive = 0;
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) {
        fputs("native_result_probe: child failed before the initial trace stop\n", stderr);
        goto failed;
    }
    if (ptrace(PTRACE_SETOPTIONS, child, NULL, (void *)(uintptr_t)options) < 0) {
        perror("native_result_probe: ptrace options unavailable");
        goto failed;
    }
    for (;;) {
        unsigned event;
        int sig;
        if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) < 0) {
            perror("native_result_probe: resume trace");
            goto failed;
        }
        if (wait_child(child, &status) < 0) {
            int saved = errno;
            perror("native_result_probe: trace wait");
            if (saved == ECHILD) alive = 0;
            goto failed;
        }
        if (WIFEXITED(status)) {
            alive = 0;
            if (!exec_seen || !exit_seen) {
                fputs("native_result_probe: no successful exec and full exit argument observed\n", stderr);
                goto failed;
            }
            if ((unsigned)WEXITSTATUS(status) != (unsigned)(exit_argument & UINT64_C(255))) {
                fputs("native_result_probe: exit status does not match the observed argument\n", stderr);
                goto failed;
            }
            *full_result = exit_argument;
            return 1;
        }
        if (WIFSIGNALED(status)) {
            alive = 0;
            fprintf(stderr, "native_result_probe: target terminated by signal %d\n", WTERMSIG(status));
            goto failed;
        }
        if (!WIFSTOPPED(status)) {
            fputs("native_result_probe: unexpected child wait state\n", stderr);
            goto failed;
        }
        if (exit_seen) {
            fputs("native_result_probe: observed exit syscall did not terminate the target\n", stderr);
            goto failed;
        }
        sig = WSTOPSIG(status);
        event = (unsigned)status >> 16;
        if (sig == SIGTRAP && event == PTRACE_EVENT_EXEC) {
            if (exec_seen) {
                fputs("native_result_probe: unexpected second exec\n", stderr);
                goto failed;
            }
            exec_seen = 1;
            continue;
        }
        if (sig == (SIGTRAP | 0x80) && event == 0) {
            struct ptrace_syscall_info info;
            long count;
            memset(&info, 0, sizeof(info));
            count = ptrace(PTRACE_GET_SYSCALL_INFO, child,
                           (void *)(uintptr_t)sizeof(info), &info);
            if (count < 0) {
                perror("native_result_probe: syscall-entry observation unavailable");
                goto failed;
            }
            if ((size_t)count < offsetof(struct ptrace_syscall_info, arch) + sizeof(info.arch) ||
                info.arch != AUDIT_ARCH_X86_64) {
                fputs("native_result_probe: unsupported syscall architecture or truncated trace data\n", stderr);
                goto failed;
            }
            if (info.op == PTRACE_SYSCALL_INFO_ENTRY) {
                struct user_regs_struct regs;
                if ((size_t)count < offsetof(struct ptrace_syscall_info, entry.args) +
                                    sizeof(info.entry.args[0])) {
                    fputs("native_result_probe: incomplete syscall-entry arguments\n", stderr);
                    goto failed;
                }
                if (exec_seen && (info.entry.nr == 60 || info.entry.nr == 231)) {
                    if (ptrace(PTRACE_GETREGS, child, NULL, &regs) < 0) {
                        perror("native_result_probe: read exit registers");
                        goto failed;
                    }
                    if ((uint64_t)regs.orig_rax != info.entry.nr ||
                        (uint64_t)regs.rdi != info.entry.args[0]) {
                        fputs("native_result_probe: inconsistent syscall and register observations\n", stderr);
                        goto failed;
                    }
                    exit_argument = (uint64_t)regs.rdi;
                    exit_seen = 1;
                }
            } else if (info.op != PTRACE_SYSCALL_INFO_EXIT) {
                fputs("native_result_probe: unknown syscall-stop phase\n", stderr);
                goto failed;
            }
            continue;
        }
        fprintf(stderr, "native_result_probe: target stopped by unexpected signal %d (event %u)\n", sig, event);
        goto failed;
    }
failed:
    if (alive) reap_failed_child(child);
    return 0;
}
int main(int argc, char **argv) {
    pid_t child, parent = getpid();
    uint64_t bits;
    int64_t result;
    if (argc != 2 || !argv[1][0]) {
        fputs("usage: native_result_probe EXECUTABLE\n", stderr);
        return 1;
    }
    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
        perror("native_result_probe: child signal disposition");
        return 1;
    }
    child = fork();
    if (child < 0) { perror("native_result_probe: fork"); return 1; }
    if (child == 0) {
        /* Close the interval before the parent can set PTRACE_O_EXITKILL. */
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != parent) {
            fputs("native_result_probe: child parent-death protection unavailable\n", stderr);
            _exit(127);
        }
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            perror("native_result_probe: own-child tracing unavailable");
            _exit(127);
        }
        if (raise(SIGSTOP) != 0) {
            perror("native_result_probe: initial stop");
            _exit(127);
        }
        execl(argv[1], argv[1], (char *)NULL);
        perror("native_result_probe: exec");
        _exit(127);
    }
    if (!trace_child(child, &bits)) return 1;
    /* Preserve the observed two's-complement bits, including INT64_MIN. */
    memcpy(&result, &bits, sizeof(result));
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("native_result_probe: result signal disposition");
        return 1;
    }
    if (printf("RESULT=%" PRId64 "\n", result) < 0 || fflush(stdout) == EOF) {
        perror("native_result_probe: result write");
        return 1;
    }
    return 0;
}
#endif
