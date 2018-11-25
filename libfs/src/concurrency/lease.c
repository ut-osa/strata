#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/un.h>
#include "lease.h"
int socket_fd;
struct mlfs_lease_struct *mlfs_lease_table = NULL;

void init_lease_client() {
  struct sockaddr_un addr;

  if ((socket_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
    perror("Socket");
    exit(1);
  }
  memset(&addr, 0, sizeof(struct sockaddr_un));

  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_NAME, sizeof(addr.sun_path) - 1);


  if (connect(socket_fd, (const struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
      perror("Connect");
      exit(1);
  }
}
char get_client_header(file_operation_t operation, inode_t type, lease_action_t act) {
    char header = 0;
    header = (header | (int)operation);
    if(type == T_DIR) {
      header = (header | ( (int)type << 2 ));
    }
    header = (header | ( (int)act << 3 ));
    return header;
}

mlfs_time_t send_requests(const char* path, file_operation_t operation, inode_t type, lease_action_t act) {

    char header = get_client_header(operation, type, act);
    printf("header:%d\n", header);
    pid_t pid = getpid();
    printf("pid:%d\n", pid);
    char send_data[data_size];
    memcpy(send_data, &header, 1);
    memcpy(send_data+sizeof(char), &pid, sizeof(pid_t));
    memcpy(send_data+sizeof(char)+sizeof(pid_t), path, strlen(path));
    printf("Send to Server:\n");
    printf("%s\n", send_data+5);

  if(send(socket_fd, send_data, sizeof(send_data), 0) < 0) {
    perror("Error when sending to Read_Socket:");
  }
  mlfs_time_t time;
  printf("Action:%d\n", act);
  if(act == acquire) {
    char receive_data[data_size];
    int count = 0;
    if((count = recv(socket_fd, receive_data, sizeof(receive_data), 0)) < 0) {
      perror("Error when receiving from Read_Socket:");
    }
    memcpy(&time, receive_data, sizeof(mlfs_time_t));
    receive_data[count] = '\0';
    printf("Received from Server:\n");
    printf("%ld %ld\n", time.tv_usec, time.tv_sec);
  }
  return time;
  

}

void shutdown_sock() {
  close(socket_fd);
}

mlfs_time_t mlfs_acquire_lease(const char* path, file_operation_t operation, inode_t type)
{
  return send_requests(path, operation, type, acquire);
}

void mlfs_release_lease(const char* path, file_operation_t operation, inode_t type)
{
  send_requests(path, operation, type, release);
}

int Acquire_lease(const char* path, mlfs_time_t* expiration_time, file_operation_t operation, inode_t type)
{
  int ret = MLFS_LEASE_OK;

  mlfs_time_t current_time;
  mlfs_get_time(&current_time);
  current_time.tv_usec += MLFS_LEASE_RENEW_THRESHOLD;
	if (timercmp(&current_time, expiration_time, <) == 0 || ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0))
	{
		  // Acquire lease if it is time to renewal or it is our first time to try to get a lease
		  do
      {
        *expiration_time = mlfs_acquire_lease(path, operation, type);

        if ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0)
        {
          // This means we hit the error
          ret = MLFS_LEASE_ERR;
        }

        if ((*expiration_time).tv_sec < 0)
        {
            ret = MLFS_LEASE_GIVE_UP;
            sleep(abs((*expiration_time).tv_sec));
        }
      } while ((*expiration_time).tv_sec < 0);
	}

  return ret;
}

int Acquire_lease_inum(uint32_t inum, mlfs_time_t* expiration_time, file_operation_t operation, inode_t type)
{
  int ret = MLFS_LEASE_OK;
  extern struct mlfs_lease_struct* mlfs_lease_table;
  struct mlfs_lease_struct *s;
  // There is no key error b/c we assume the file is opened (and thus the inum is added
  // to the mlfs_lease_table) before the file is written
  HASH_FIND_INT(mlfs_lease_table, &inum, s);

  mlfs_time_t current_time;
  mlfs_get_time(&current_time);
  current_time.tv_usec += MLFS_LEASE_RENEW_THRESHOLD;
	if (timercmp(&current_time, expiration_time, <) == 0 || ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0))
	{
		  // Acquire lease if it is time to renewal or it is our first time to try to get a lease
		  do
      {
        *expiration_time = mlfs_acquire_lease(s->path, operation, type);

        if ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0)
        {
          // This means we hit the error
          ret = MLFS_LEASE_ERR;
        }

        if ((*expiration_time).tv_sec < 0)
        {
            ret = MLFS_LEASE_GIVE_UP;
            sleep(abs((*expiration_time).tv_sec));
        }
      } while ((*expiration_time).tv_sec < 0);
	}

  return ret;
}



