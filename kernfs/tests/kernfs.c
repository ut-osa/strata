#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "kernfs_interface.h"

static inline void safe_exit() {
    shutdown_fs();
    remove(KERNFSPIDPATH);
    exit(0);
}

static void sig_handler(int signum) {
    printf("SIGNAL is %d\n", signum);
    switch (signum) {
        case SIGINT:
            show_kernfs_stats();
            break;
        case SIGUSR2:
            show_kernfs_stats();
            reset_kernfs_stats();
            break;
        case SIGQUIT:
            safe_exit();
        default:
            ;
    }
}

static void regist_sighandler() {
    struct sigaction new_action;
    new_action.sa_handler = sig_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    if (sigaction(SIGINT, &new_action, NULL) != 0) {
        perror("SIGINT error");
        exit(-1);
    }
    if(sigaction(SIGUSR2, &new_action, NULL) != 0) {
        perror("SIGUSR1 error");
        exit(-1);
    }
    if (sigaction(SIGQUIT, &new_action, NULL) != 0) {
        perror("SIGQUIT error");
        exit(-1);
    }
}

static void write_pid(char *path) {
    pid_t pid = getpid();
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        perror("open pidfile failed");
        exit(-1);
    }
    fprintf(f, "%d\n", pid);
    fclose(f);
}

int main(void)
{
    regist_sighandler();
    write_pid(KERNFSPIDPATH);
	printf("initialize file system\n");

	init_fs();

    safe_exit();
}
