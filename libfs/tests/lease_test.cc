/*************************************************************************
  > File Name:       lease_test.cc
  > Author:          Zeyuan Hu
  > Mail:            iamzeyuanhu@utexas.edu
  > Created Time:    10/31/18
  > Description:

    Driver program for the lease test sequence
************************************************************************/

#include <cassert>
#include <cstdlib>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
#include <chrono>
#include <thread>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef MLFS
#include <mlfs/mlfs_interface.h>
#endif

#include "storage/spdk/async.h"
#include "storage/spdk/sync.h"

//#include "test_primitive.h"
#include "thread.h"
#include "time_stat.h"

namespace StrataLease {

#ifdef MLFS
const char test_dir_prefix[] = "/mlfs/";
#else
const char test_dir_prefix[] = "./t";
#endif

#define BUFFER_SIZE 10000
char buffer[10000];

typedef enum {
    SEQ_WRITE,
    SEQ_READ,
    SEQ_WRITE_READ,
    RAND_WRITE,
    RAND_READ,
    NONE
} test_t;

using namespace std;

ostream& PIDLOG()
{
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%X", &tstruct);
    return std::cout << '[' << getpid() << "][" << buf << "]";
}
std::string Strerror(int err)
{
    return std::string("{") + strerror(err) + "}";
}
void CHECK_FD(const std::string & filename, int fd) {
    if (fd > 0) {
        PIDLOG() << "open " << filename << " succeeded." << std::endl;
    } else {
        PIDLOG() << "open " << filename << " failed with " << Strerror(-fd) << std::endl;
    }
}
bool makeDir(const std::string& dirname, int expection)
{
    int ret = mkdir(dirname.c_str(), 0700);
    PIDLOG() << "makeDir " << dirname << " finished with " << Strerror(ret) << std::endl;
    return ret == expection;
}
bool removeDir(const std::string& dirname, int expection)
{
    int ret = unlink(dirname.c_str());
    PIDLOG() << "removeDir " << dirname << " finished with " << Strerror(ret) << std::endl;
    return ret == expection;
}
bool removeFile(const std::string& filename, int expection)
{
    int ret = unlink(filename.c_str());
    PIDLOG() << "removeFile " << filename << " finished with " << Strerror(ret) << std::endl;
    return ret == expection;
}
bool writeFile(const std::string& filename, const std::string& verify_filename, int expection)
{
    std::stringstream ss;
    std::ifstream fin(verify_filename);
    ss << fin.rdbuf();
    fin.close();
    const std::string filecontent = ss.str();

    //PIDLOG() << "Verify filecontent loaded. Prepare to open read file" << std::endl;
    int fd = open(filename.c_str(), O_RDWR | O_CREAT, 0600);
    //PIDLOG() << "File opened with fd " << fd << std::endl;
    CHECK_FD(filename, fd);
    size_t filesize = filecontent.size();
    size_t size = write(fd, filecontent.c_str(), filesize);
    close(fd);
    PIDLOG() << "writeFile " << filename << " finished with " << size << " written." << std::endl;
    return errno == expection;
}
bool readFile(const std::string& filename, const std::string& verify_filename, int expection)
{
    std::stringstream ss;
    std::ifstream fin(verify_filename);
    ss << fin.rdbuf();
    fin.close();
    const std::string filecontent = ss.str();
    //PIDLOG() << "Verify filecontent loaded. Prepare to open read file" << std::endl;
    int fd = open(filename.c_str(), O_RDWR, 0600);
    //PIDLOG() << "File opened with fd " << fd << std::endl;
    CHECK_FD(filename, fd);
    memset(buffer, 0, BUFFER_SIZE);
    size_t filesize = filecontent.size();
    //PIDLOG() << "Trying to read from fd" << std::endl;
    size_t size = read(fd, buffer, filesize);
    close(fd);
    PIDLOG() << "readFile " << filename << " finished with " << size << " read." << std::endl;
    if (memcmp(filecontent.c_str(), buffer, filesize)) {
        PIDLOG() << "filecontent verify failed." << std::endl;
    }
    return errno == expection;
}
void sleep(int seconds)
{
    PIDLOG() << "Sleep for " << seconds << " seconds" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    PIDLOG() << "Sleep finished after " << seconds << " seconds" << std::endl;
}
std::vector<std::string> split(const std::string& str,
    const std::string& delim)
{
    std::vector<std::string> tokens;
    size_t prev = 0, pos = 0;
    do {
        pos = str.find(delim, prev);
        if (pos == std::string::npos)
            pos = str.length();
        std::string token = str.substr(prev, pos - prev);
        if (!token.empty())
            tokens.push_back(token);
        prev = pos + delim.length();
    } while (pos < str.length() && prev < str.length());
    return tokens;
}

void show_usage(string prog, string err_cmd, int lineno)
{
    std::cerr << "usage: " << prog << " test_suite.ini" << endl;
    cerr << "test_suite.ini support format: " << endl
         << "mkdir <dirname> <expect result>" << endl
         << "read <filename> <verify filename> <expect result>" << endl
         << "write <filename> <verify filename> <expect result>" << endl
         << "rm <filename> <expect result>" << endl
         << "rmdir <dirname> <expect result>" << endl
         << "sleep <seconds>" << endl;
    if (err_cmd == "MISS") {
        cerr << "Missing test_suite.ini" << endl;
    } else if (err_cmd == "mkdir") {
        cerr << "mkdir error: " << to_string(lineno) << endl;
    } else if (err_cmd == "read") {
        cerr << "read error: " << to_string(lineno) << endl;
    } else if (err_cmd == "write") {
        cerr << "write error: " << to_string(lineno) << endl;
    } else if (err_cmd == "rm") {
        cerr << "rm error: " << to_string(lineno) << endl;
    } else if (err_cmd == "rmdir") {
        cerr << "rmdir error: " << to_string(lineno) << endl;
    } else if (err_cmd == "sleep") {
        cerr << "sleep error: " << to_string(lineno) << endl;
    } else {
        cerr << "Unknown cmd: " << to_string(lineno) << endl;
    }
}

void parse_suite(string prog, string filename)
{
    ifstream inputFile(filename);
    string line;
    int lineno = 0;
    while (getline(inputFile, line)) {
        auto tokens = split(line, " ");
        if (tokens[0] == "mkdir") {
            if (tokens.size() != 3) {
                show_usage(prog, "mkdir", lineno);
                exit(-1);
            }
            StrataLease::makeDir(tokens[1], stoi(tokens[2]));
        } else if (tokens[0] == "read") {
            if (tokens.size() != 4) {
                show_usage(prog, "read", lineno);
                exit(-1);
            }
            StrataLease::readFile(tokens[1], tokens[2], stoi(tokens[3]));
        } else if (tokens[0] == "write") {
            if (tokens.size() != 4) {
                show_usage(prog, "write", lineno);
                exit(-1);
            }
            StrataLease::writeFile(tokens[1], tokens[2], stoi(tokens[3]));
        } else if (tokens[0] == "rm") {
            if (tokens.size() != 3) {
                show_usage(prog, "rm", lineno);
                exit(-1);
            }
            StrataLease::removeFile(tokens[1], stoi(tokens[2]));
        } else if (tokens[0] == "rmdir") {
            if (tokens.size() != 3) {
                show_usage(prog, "rmdir", lineno);
                exit(-1);
            }
            StrataLease::removeDir(tokens[1], stoi(tokens[2]));
        } else if (tokens[0] == "sleep") {
            if (tokens.size() != 2) {
                show_usage(prog, "sleep", lineno);
                exit(-1);
            }
            StrataLease::sleep(stoi(tokens[1]));
        } else {
            show_usage(prog, "", lineno);
            exit(-1);
        }
        lineno++;
    }
}
}

int main(int argc, char* argv[])
{

    if (argc != 2) {
        StrataLease::show_usage(argv[0], "MISS", 0);
        exit(-1);
    }

    init_fs();
    StrataLease::parse_suite(argv[0], argv[1]);
    shutdown_fs();
    return 0;
}
