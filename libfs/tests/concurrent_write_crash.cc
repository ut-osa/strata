/*************************************************************************
  > File Name:       concurrent_write_crash.cc
  > Author:          Zeyuan Hu
  > Mail:            ferrishu3886@gmail.com
  > Created Time:    11/10/18
  > Description:
    
    This benchmark simulate the case when one of the processes that writing to
    the file is crashed while holding the lease.
 ************************************************************************/

#include <cassert>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <unordered_map>
#include <cstdlib>
#include <stdlib.h>
#include <fstream>

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>


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
  cerr << "usage: " << prog << " .ini" << endl;
}


int
main(int argc, char **argv)
{
    if (argc != 2) {
        show_usage(argv[0]);
        exit(-1);
    }

    string filename = argv[1];
    ifstream infile(filename);
    string line, cmd;
    vector<string> cmds;
    while(getline(infile, line))
    {
      cout << "line: " << line << endl;
      cmds.push_back(line);
    }

    int numProcesses = cmds.size();
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
            cmd = cmds[i] + " > /tmp/log" + to_string(i);
            cmd = string("bash -c ") + "\"" + cmd + "\"";
            cout << "CMD" + to_string(i) + ": " + cmd << endl;
            //system(cmd.c_str());
            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *) 0);
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
