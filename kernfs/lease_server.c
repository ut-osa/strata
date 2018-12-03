#include "lease_server.h"
#include <time.h>
static int socket_fd, epoll_fd;
struct sockaddr_un lease_server_addr;
char* action_str[2] = { "acquire", "release" };
char* operation_str[4] = { "mlfs_read_op", "mlfs_write_op", "mlfs_create_op", "mlfs_delete_op" };
char* type_str[2] = { "T_FILE", "T_DIR" };

static void socket_create_bind_local()
{

    unlink(LEASE_SOCKET_NAME);

    if ((socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
        perror("Socket");
        exit(1);
    }

    memset(&lease_server_addr, 0, sizeof(struct sockaddr_un));
    lease_server_addr.sun_family = AF_UNIX;
    strncpy(lease_server_addr.sun_path, LEASE_SERVER_NAME, sizeof(lease_server_addr.sun_path));
    unlink(LEASE_SERVER_NAME);

    if (bind(socket_fd, (const struct sockaddr*)&lease_server_addr,
            sizeof(struct sockaddr_un))
        < 0) {
        perror("Unable to bind");
        exit(1);
    }
}

static int make_socket_non_blocking(int socket_fd)
{
    int flags;

    flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(socket_fd, F_SETFL, flags) < 0) {
        perror("fcntl");
        return -1;
    }

    return 0;
}

struct mlfs_lease_call get_header(char c)
{
    struct mlfs_lease_call header;
    header.operation = (c & 3); // 3 = 0x11;
    if ((c & 4) == 0) {
        header.type = T_FILE;
    } else {
        header.type = T_DIR;
    }
    header.action = (c & 8); // 8 = 0x1000
    return header;
}
void CurrentTime(char* timeBuffer)
{
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    int hour = timeinfo->tm_hour;
    int minute = timeinfo->tm_min;
    int second = timeinfo->tm_sec;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    sprintf(timeBuffer, "%d:%d:%d.%06ld", hour, minute, second, tv.tv_usec);
    //mlfs_info( " [ %d:%d:%d ] ", hour, minute, second);
}
void myTimeval(struct timeval tv, char* timeBuffer)
{
    time_t nowtime;
    struct tm* nowtm;
    char tmbuf[64];

    nowtime = tv.tv_sec;
    if (nowtime < 0)
        nowtime = (-1) * nowtime;
    nowtm = localtime(&nowtime);
    strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", nowtm);
    if (tv.tv_sec >= 0)
        sprintf(timeBuffer, "%s.%ld", tmbuf, tv.tv_usec);
    else
        sprintf(timeBuffer, "[*]%s.%ld", tmbuf, tv.tv_usec);
    // mlfs_info("Received from Server: %s\n", buf);
    //
}
void process_new_data(int fd)
{
    ssize_t count;
    char buf[buf_size]; //header 1byte, path 4096bytes
    struct sockaddr_un cli_addr;
    socklen_t len = sizeof(struct sockaddr_un);
    mlfs_info("%s\n", "process_new_data");

    while ((count = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *) &cli_addr, &len))) {
        if (count == -1) {
            if (errno == EAGAIN)
                return;
            perror("read");
            break;
        } else if(count == 0) {
            return;
        }
        buf[count] = '\0';
        char timeBuffer[60];
        CurrentTime(timeBuffer);
        //mlfs_info("Message Size: %ld\n", count);
        struct mlfs_lease_call header = get_header(buf[0]);
        pid_t pid;
        memcpy(&pid, buf + sizeof(char), sizeof(pid_t));
        char path[path_size];
        memcpy(path, buf + sizeof(char) + sizeof(pid_t), sizeof(path));
        path[count - sizeof(char) - sizeof(pid_t)] = '\0';
        mlfs_info("[%s] Receive from client %d: %s %s %s (%s)\n", timeBuffer, pid, header.action == acquire ? action_str[0] : action_str[1], operation_str[header.operation], header.type == T_FILE ? type_str[0] : type_str[1], path);
        //mlfs_info("[%s] Receive from client: %s %s %s (%s) \n", timeBuffer, action_str[0], operation_str[header.operation], header.type == T_FILE ? type_str[0] : type_str[1], path);

        if (header.action == acquire) {
            char send_data[buf_size];
            mlfs_time_t time = lease_acquire((const char*)path, header.operation, header.type, pid);
            memcpy(send_data, &time, sizeof(mlfs_time_t));
            char timeBuf[60];
            myTimeval(time, timeBuf);
            char timeBuffer2[60];
            CurrentTime(timeBuffer2);
            mlfs_info("[%s] Timeval received from lease_manager: %s\n", timeBuffer2, timeBuf);
            if (sendto(fd, &send_data, sizeof(mlfs_time_t), 0, (struct sockaddr *)&cli_addr, len) == -1) {
                perror("send");
                break;
            }
        } else {
            lease_release((const char*)path, header.operation, header.type, pid);
        }
    }

    mlfs_info("Close connection on descriptor: %d\n", fd);
    close(fd);
}

void run_lease_server(void* arg)
{
    struct epoll_event event, *events;

    socket_create_bind_local();

    if (make_socket_non_blocking(socket_fd) == -1)
        exit(1);

    //mlfs_info("\nServer Waiting for client%c\n", ' ');
    fflush(stdout);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(1);
    }

    event.data.fd = socket_fd;
    event.events = EPOLLIN | EPOLLET | EPOLLHUP | EPOLLRDHUP;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) < 0) {
        perror("epoll_ctl");
        exit(1);
    }

    events = mlfs_zalloc(MAXEVENTS * sizeof(struct epoll_event));

    while (1) {
        int n, i;
        n = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
        if (n < 0 && errno != EINTR)
            panic("epoll has error\n");
        for (i = 0; i < n; i++) {
            if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP || !(events[i].events & EPOLLIN)) {
                /* An error on this fd or socket not ready */
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("epoll error");
                    close(events[i].data.fd);
                }
            } else if (events[i].events & EPOLLIN && events[i].data.fd == socket_fd) {
                process_new_data(socket_fd);
            }
        }
    }

    free(events);
    close(socket_fd);
    unlink(LEASE_SOCKET_NAME);
}
