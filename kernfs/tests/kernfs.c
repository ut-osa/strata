#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include "kernfs_interface.h"
void sig_handler(int signum) {
    switch (signum) {
        case SIGINT:
            show_kernfs_stats();
            break;
        case SIGUSR1:
            reset_kernfs_stats();
            break;
        case SIGQUIT:
            shutdown_fs();
            exit(0);
            break;
        default:
            ;
    }
}
int main(void)
{
    struct sigaction new_action;
    new_action.sa_handler = sig_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGINT, &new_action, NULL);
    sigaction(SIGUSR1, &new_action, NULL);
    sigaction(SIGQUIT, &new_action, NULL);
	printf("initialize file system\n");

	init_fs();

	shutdown_fs();
}
