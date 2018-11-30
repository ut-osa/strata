#include "lease_server.h"
#include <time.h>
static int socket_fd, epoll_fd;
struct sockaddr_un name;
char* action_str[2] = { "acquire", "release" };
char* operation_str[4] = { "mlfs_read_op", "mlfs_write_op", "mlfs_create_op", "mlfs_delete_op" };
char* type_str[2] = { "T_FILE", "T_DIR" };

static void socket_create_bind_local() {

	unlink(SOCKET_NAME);

	if ((socket_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
		perror("Socket");
		exit(1);
	}
	memset(&name, 0, sizeof(struct sockaddr_un));

	name.sun_family = AF_UNIX;
	strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);

	if(bind(socket_fd, (const struct sockaddr *) &name,
		sizeof(struct sockaddr_un)) == -1) {
		perror("Unable to bind");
		exit(1);
	}

}

static int make_socket_non_blocking(int sfd) {
	int flags;

	flags = fcntl(sfd, F_GETFL, 0);
	if (flags == -1) {
		perror("fcntl");
		return -1;
	}

	flags |= O_NONBLOCK;
	if (fcntl(sfd, F_SETFL, flags) == -1) {
		perror("fcntl");
		return -1;
	}

	return 0;
}

void accept_and_add_new() {
	struct epoll_event event;
	struct sockaddr in_addr;
	socklen_t in_len = sizeof(in_addr);
	int infd;

	while ((infd = accept(socket_fd, &in_addr, &in_len)) != -1) {
		mlfs_info("Add new Connection%c\n", ' ');
		if (make_socket_non_blocking(infd) == -1) {
			abort();
		}

		event.data.fd = infd;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, infd, &event) == -1) {
			perror("epoll_ctl");
			abort();
		}
		in_len = sizeof(in_addr);
	}

	if (errno != EAGAIN && errno != EWOULDBLOCK)
		perror("accept");
}
// struct mlfs_lease_call {
//     lease_action_t action;
//     const char* path;
//     file_operation_t operation;
//     inode_t type;
// };

struct mlfs_lease_call get_header(char c) {
	struct mlfs_lease_call header;
	header.operation = (c & 3); // 3 = 0x11;
	if((c & 4) == 0) {
		header.type = T_FILE;
	} else {
		header.type = T_DIR;
	}
	header.action = (c & 8);// 8 = 0x1000
	return header;
}
void CurrentTime(char* timeBuffer) {
    time_t rawtime;
    struct tm * timeinfo;
    time ( &rawtime  );
    timeinfo = localtime ( &rawtime  );
    int hour = timeinfo->tm_hour;
    int minute = timeinfo->tm_min;
    int second = timeinfo->tm_sec;
    struct timeval tv;
    gettimeofday(&tv,NULL);
    sprintf ( timeBuffer,  "%d:%d:%d.%06ld", hour, minute, second , tv.tv_usec );
    //mlfs_info( " [ %d:%d:%d ] ", hour, minute, second);
}
void process_new_data(int fd) {
	ssize_t count;
	char buf[buf_size];//header 1byte, path 4096bytes 

	while ((count = read(fd, buf, sizeof(buf) ))) {
		if (count == -1) {
			if (errno == EAGAIN)
				return;
			perror("read");
			break;
		}
        buf[count] = '\0';
        char timeBuffer[60];
        CurrentTime(timeBuffer);
		//mlfs_info("Message Size: %ld\n", count);
		struct mlfs_lease_call header = get_header(buf[0]);
		pid_t pid;
		memcpy(&pid, buf+sizeof(char), sizeof(pid_t));
		char path[path_size];
		memcpy(path, buf+sizeof(char)+sizeof(pid_t), sizeof(path));
        path[count-sizeof(char)-sizeof(pid_t)] = '\0';
        mlfs_info("[%s] Receive from client: %s %s %s (%s)\n", timeBuffer, header.action == acquire? action_str[0]: action_str[1], operation_str[header.operation], header.type == T_FILE? type_str[0]:type_str[1], path);
        //mlfs_info("[%s] Receive from client: %s %s %s (%s) \n", timeBuffer, action_str[0], operation_str[header.operation], header.type == T_FILE ? type_str[0] : type_str[1], path);

		if(header.action == acquire) {
			char send_data[buf_size];
			mlfs_time_t time = lease_acquire ((const char*)path, header.operation, header.type, pid);
			memcpy(send_data, &time, sizeof(mlfs_time_t));
			if(send(fd, &send_data, sizeof(mlfs_time_t), 0) == -1) {
				perror("send");
				break;
			}
		} else {
			lease_release((const char*) path, header.operation, header.type, pid);
		}
		
	}
	
	mlfs_info("Close connection on descriptor: %d\n", fd);
	close(fd);
}

void run_lease_server(void *arg) {
	struct epoll_event event, *events;

	socket_create_bind_local();

	if (make_socket_non_blocking(socket_fd) == -1)
		exit(1);

	if (listen(socket_fd, 20) == -1) {
		perror("Listen");
		exit(1);
	}

	//mlfs_info("\nServer Waiting for client%c\n", ' ');
	fflush(stdout);

	epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
		perror("epoll_create1");
		exit(1);
	}

	event.data.fd = socket_fd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) == -1) {
		perror("epoll_ctl");
		exit(1);
	}

	events = calloc(MAXEVENTS, sizeof(event));

	while(1) {
		int n, i;
		n = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
		for (i = 0; i < n; i++) {
			if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP ||
				!(events[i].events & EPOLLIN)) {
				/* An error on this fd or socket not ready */
				if (errno != EAGAIN && errno != EWOULDBLOCK) {
					perror("epoll error");
					close(events[i].data.fd);
				}
			} else if (events[i].data.fd == socket_fd) {
				/* New incoming connection */
				accept_and_add_new();
			} else {
				/* Data incoming on fd */
				process_new_data(events[i].data.fd);
			}
		}
	}

	free(events);
	close(socket_fd);
	unlink(SOCKET_NAME);
}

