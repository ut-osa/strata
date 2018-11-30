#include "lease.h"
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
int socket_fd;
struct mlfs_lease_struct *mlfs_lease_table = NULL;

char *action_string[2] = {"acquire", "release"};
char *operation_string[4] = {"mlfs_read_op", "mlfs_write_op", "mlfs_create_op",
                             "mlfs_delete_op"};
char *type_string[2] = {"T_FILE", "T_DIR"};

void init_lease_client() {
  struct sockaddr_un addr;

  if ((socket_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
    perror("Socket");
    exit(1);
  }
  memset(&addr, 0, sizeof(struct sockaddr_un));

  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_NAME, sizeof(addr.sun_path) - 1);

  if (connect(socket_fd, (const struct sockaddr *)&addr,
              sizeof(struct sockaddr_un)) == -1) {
    perror("Connect");
    exit(1);
  }
}
char get_client_header(file_operation_t operation, inode_t type,
                       lease_action_t act) {
  char header = 0;
  header = (header | (int)operation);
  if (type == T_DIR) {
    header = (header | ((int)type << 2));
  }
  header = (header | ((int)act << 3));
  return header;
}
void Time(char *timeBuffer) {
  time_t rawtime;
  struct tm *timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;
  int second = timeinfo->tm_sec;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  sprintf(timeBuffer, "%d:%d:%d.%06ld", hour, minute, second, tv.tv_usec);
  // mlfs_info(" [ %d:%d:%d ] ", hour, minute, second);
}
void Timeval(struct timeval tv, char *timeBuffer) {
  time_t nowtime;
  struct tm *nowtm;
  char tmbuf[64];

  nowtime = tv.tv_sec;
  nowtm = localtime(&nowtime);
  strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", nowtm);
  sprintf(timeBuffer, "%s.%06ld", tmbuf, tv.tv_usec);
  // mlfs_info("Received from Server: %s\n", buf);
}
mlfs_time_t send_requests(const char *path, file_operation_t operation,
                          inode_t type, lease_action_t act) {

  char header = get_client_header(operation, type, act);
  char current_time[60];
  Time(current_time);
  mlfs_info("[%s] %s %s %s : (%s)\n", current_time,
            act == acquire ? action_string[0] : action_string[1],
            operation_string[operation],
            type == T_FILE ? type_string[0] : type_string[1], path);
  pid_t pid = getpid();
  char send_data[data_size];
  memcpy(send_data, &header, 1);
  memcpy(send_data + sizeof(char), &pid, sizeof(pid_t));
  memcpy(send_data + sizeof(char) + sizeof(pid_t), path, strlen(path));

  if (send(socket_fd, send_data,
           sizeof(char) + sizeof(pid_t) + strlen(path) + 1, 0) < 0) {
    perror("Error when sending to Read_Socket:");
  }
  mlfs_time_t time;
  if (act == acquire) {
    char receive_data[data_size];
    int count = 0;
    if ((count = recv(socket_fd, receive_data, sizeof(receive_data), 0)) < 0) {
      perror("Error when receiving from Read_Socket:");
    }
    memcpy(&time, receive_data, sizeof(mlfs_time_t));
    receive_data[count] = '\0';
    Time(current_time);
    char next_time[60];
    Timeval(time, next_time);
    mlfs_info("[%s] Recived from server: %s\n", current_time, next_time);
  }
  return time;
}

void shutdown_sock() { close(socket_fd); }

mlfs_time_t mlfs_acquire_lease(const char *path, file_operation_t operation,
                               inode_t type) {
  return send_requests(path, operation, type, acquire);
}

void mlfs_release_lease(const char *path, file_operation_t operation,
                        inode_t type) {
  send_requests(path, operation, type, release);
}

void mlfs_release_lease_inum(uint32_t inum, file_operation_t operation,
                             inode_t type) {
  extern struct mlfs_lease_struct *mlfs_lease_table;
  struct mlfs_lease_struct *s;
  // There is no key error b/c we assume the file is opened (and thus the inum
  // is added
  // to the mlfs_lease_table) before the file is written
  HASH_FIND_INT(mlfs_lease_table, &inum, s);
  mlfs_release_lease(s->path, operation, type);
}

int Acquire_lease(const char *path, mlfs_time_t *expiration_time,
                  file_operation_t operation, inode_t type) {
  int ret = MLFS_LEASE_OK;

  mlfs_time_t current_time;
  mlfs_get_time(&current_time);
  current_time.tv_usec += MLFS_LEASE_RENEW_THRESHOLD;
  if (timercmp(&current_time, expiration_time, <) == 0 ||
      ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0)) {
    // Acquire lease if it is time to renewal or it is our first time to try to
    // get a lease
    mlfs_info("Inside if of Acquire_lease: %c\n", ' ');
    do {
      *expiration_time = mlfs_acquire_lease(path, operation, type);

      if ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0) {
        // This means we hit the error
        make_digest_request_sync(MLFS_LEASE_PERCENT);
        ret = MLFS_LEASE_ERR;
      }

      if ((*expiration_time).tv_sec < 0) {
        ret = MLFS_LEASE_GIVE_UP;
        sleep(abs((*expiration_time).tv_sec));
      }
    } while ((*expiration_time).tv_sec < 0);
  }

  if (ret == MLFS_LEASE_GIVE_UP && operation == mlfs_read_op)
  {
    // ret == MLFS_LEASE_GIVE_UP indicates that we slept before. This means that
    // there is some other process that has acquired the write lease of the sa
    // me file before. We have acquired the read lease now. We will request
    // digest so that our read lease can read the latest file change done
    // by some other process.
    make_digest_request_sync(MLFS_LEASE_PERCENT);
  }

  return ret;
}

int Acquire_lease_inum(uint32_t inum, mlfs_time_t *expiration_time,
                       file_operation_t operation, inode_t type) {
  int ret = MLFS_LEASE_OK;
  extern struct mlfs_lease_struct *mlfs_lease_table;
  struct mlfs_lease_struct *s;
  // There is no key error b/c we assume the file is opened (and thus the inum
  // is added
  // to the mlfs_lease_table) before the file is written
  HASH_FIND_INT(mlfs_lease_table, &inum, s);

  mlfs_time_t current_time;
  mlfs_get_time(&current_time);
  /* mlfs_info("expiration_time: %ld, %lu\n", expiration_time->tv_sec,
   * expiration_time->tv_usec); */
  /* mlfs_info("current_time: %ld, %lu\n", current_time.tv_sec,
   * current_time.tv_usec);     */
  current_time.tv_usec += MLFS_LEASE_RENEW_THRESHOLD;
  if (timercmp(&current_time, expiration_time, <) == 0 ||
      ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0)) {
    // Acquire lease if it is time to renewal or it is our first time to try to
    // get a lease
    mlfs_info("Inside if of Acquire_lease_inum: %c\n", ' ');
    do {
      *expiration_time = mlfs_acquire_lease(s->path, operation, type);

      if ((*expiration_time).tv_sec == 0 && (*expiration_time).tv_usec == 0) {
        // This means we hit the error (file re-created or deleted). However,
        // we want to bring the file status to the latest
        make_digest_request_sync(MLFS_LEASE_PERCENT);
        ret = MLFS_LEASE_ERR;
      }

      if ((*expiration_time).tv_sec < 0) {
        ret = MLFS_LEASE_GIVE_UP;
        sleep(abs((*expiration_time).tv_sec));
      }
    } while ((*expiration_time).tv_sec < 0);
  }

  if (ret == MLFS_LEASE_GIVE_UP && operation == mlfs_read_op)
  {
    // ret == MLFS_LEASE_GIVE_UP indicates that we slept before. This means that
    // there is some other process that has acquired the write lease of the sa
    // me file before. We have acquired the read lease now. We will request
    // digest so that our read lease can read the latest file change done
    // by some other process.
    make_digest_request_sync(MLFS_LEASE_PERCENT);
  }
  
  return ret;
}
