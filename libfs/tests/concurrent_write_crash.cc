/*************************************************************************
  > File Name:       concurrent_write_crash.cc
  > Author:          Zeyuan Hu
  > Mail:            ferrishu3886@gmail.com
  > Created Time:    11/10/18
  > Description:
    
    This benchmark simulate the case when one of the processes that writing to
    the file is crashed while holding the lease.
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <unordered_map>
#include <vector>
#include <string>

using namespace std;

#define BUF_SIZE 4096

long int
calculate_time(struct timeval* t_before, struct timeval* t_after)
{
    if (t_after->tv_usec < t_before->tv_usec)
    {
        t_after->tv_usec += 1000000;
        t_after->tv_sec -= 1;
    }
    return (t_after->tv_sec - t_before->tv_sec)*1000000 + (t_after->tv_usec - t_before->tv_usec);
}

int
DoWrite(const char *filename)
{
    char buffer[BUF_SIZE];
    sprintf(buffer, "file-write-O_CREATE 0\n");
    int fd = open(filename, O_RDWR | O_CREAT, 0777);
    assert(fd > 0);
    write(fd, buffer, strlen(buffer));
    close(fd);
    return 0;
}

void show_usage(const char *prog) {
  std::cerr << "usage: " << prog << "cmd" << endl;
}


int
main(int argc, char **argv)
{
    if (argc != 2) {
        io_fork::show_usage(argv[0]);
        exit(-1);
    }
    cout << argv[1] << endl;

    string filename = "/tmp/testfile";
    int fd = open(filename.c_str(), O_RDWR | O_CREAT, 0777);
    assert(fd > 0);

    int numProcesses = 2;
    pid_t pids[numProcesses];
    // <before, after>
    vector<pair<struct timeval*, struct timeval*>> time;
    for(int i = 0; i < numProcesses; ++i)
    {
        time.emplace_back(make_pair((struct timeval *) malloc(sizeof(struct timeval)), (struct timeval *) malloc(sizeof(struct timeval))));
    }
    // <pid, idx of the time>
    unordered_map<int, int> table;


    /* Start children. */
    for (int i = 0; i < numProcesses; ++i)
    {
        if ((pids[i] = fork()) < 0)
        {
            perror("fork");
            abort();
        }
        else if (pids[i] == 0)
        {
            // child process
            printf("[son] pid %d from [parent] pid %d\n", getpid(), getppid());
            table[getpid()] = i;
            //DoWrite(filename.c_str());
            string cmd = string("./run.sh iotest sr 100M 4K 1 > ") + "/tmp/log" + to_string(i);
            system(cmd.c_str());
            exit(0);
        }
        else
        {
            // parent process
            gettimeofday(time[i].first, NULL);
        }
    }

    /* Wait for children to exit. */
    int status;
    pid_t pid;
    while (numProcesses > 0)
    {
        pid = wait(&status);
        gettimeofday(time[table[pid]].second, NULL);
        printf("Child with PID %ld \t status %x \t time: %ld us\n",
               (long) pid,
               status,
               calculate_time(time[table[pid]].first, time[table[pid]].second));
        --numProcesses;  // TODO(pts): Remove pid from the pids array.
    }

    return 0;
}
