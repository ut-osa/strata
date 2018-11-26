#ifndef _LEASE_SERVER_H
#define _LEASE_SERVER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <errno.h>
#include <sys/un.h>
#include "filesystem/shared.h"
#include "lease_manager.h"

#define buf_size 50100
#define path_size 4097
#define MAXEVENTS 64
#define SOCKET_NAME "/tmp/mlfs-lease/connection.socket"

// mlfs_time_t lease_acquire(const char *path, file_operation_t operation, inode_t type, pid_t client);
// // static void socket_create_bind_local();
// // static void socket_create_bind_local();
// // static int make_socket_non_blocking(int sfd);
// // void accept_and_add_new();
// // struct mlfs_lease_call get_header(char c);
// // void process_new_data(int fd);
void run_server(void *arg);
#endif
