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
#define LEASE_SOCKET_NAME "/tmp/mlfs-lease/connection.socket"


void run_lease_server(void *arg);
#endif
