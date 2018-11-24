#include "acutest.h"
#include "filesystem/shared.h"
#include "lease_manager.h"
#include <sys/time.h>
extern mlfs_time_t lease_error;

void test_acquire_lease()
{
    // acquire two lease on different path
    init_lease_global();
    const char *p1 = "/tmp/file1";
    const char *p2 = "/tmp/file2";
    pid_t client0 = 0, client1 = 1;
    mlfs_time_t t0, t1, current_time;
    t0 = lease_acquire(p1, mlfs_read_op, T_FILE, client0);
    gettimeofday(&current_time, NULL);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(timercmp(&t0, &current_time, >));
    t1 = lease_acquire(p2, mlfs_write_op, T_FILE, client0);
    TEST_CHECK(t1.tv_sec > 0 && t1.tv_usec > 0);
    TEST_CHECK(timercmp(&t1, &current_time, >));
}

void test_read_lease_contention()
{
    // two concurrent read, then a write occurs
    init_lease_global();
    pid_t client0 = 0, client1 = 1, client2 = 2;
    const char *p1 = "/tmp/file1";
    mlfs_time_t t0, t1, t2;
    t0 = lease_acquire(p1, mlfs_read_op, T_FILE, client0);
    t1 = lease_acquire(p1, mlfs_read_op, T_FILE, client1);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(timercmp(&t0, &t1, <=));
    t2 = lease_acquire(p1, mlfs_write_op, T_FILE, client2);
    // should get an expiration time
    TEST_CHECK(t2.tv_sec == -t1.tv_sec && t2.tv_usec == t1.tv_usec);
}

void test_write_lease_contention()
{
    // Two clients want to write on the same file
    init_lease_global();
    pid_t client0 = 0, client1 = 1;
    const char *p1 = "/tmp/file1";
    mlfs_time_t t0, t1, t2;
    t0 = lease_acquire(p1, mlfs_write_op, T_FILE, client0);
    t1 = lease_acquire(p1, mlfs_write_op, T_FILE, client1);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(t1.tv_sec < 0);
    TEST_CHECK(t1.tv_sec == -t0.tv_sec && t1.tv_usec == t0.tv_usec);
}

void test_release_write_lease()
{
    // another write should be able to proceed if the previous write give up
    // the previous write lease
    init_lease_global();
    mlfs_time_t t0, t1, t2;
    pid_t client0 = 0, client1 = 1;
    const char *p1 = "/tmp/file1";
    t0 = lease_acquire(p1, mlfs_write_op, T_FILE, client0);
    t1 = lease_acquire(p1, mlfs_write_op, T_FILE, client1);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(t1.tv_sec == -t0.tv_sec && t1.tv_usec == t0.tv_usec);
    t1.tv_sec = -t1.tv_sec;
    lease_release(p1, mlfs_write_op, T_FILE, client0);
    t2 = lease_acquire(p1, mlfs_write_op, T_FILE, client1);
    TEST_CHECK(t1.tv_sec > 0);
    TEST_CHECK(t2.tv_sec > 0);
    TEST_CHECK(t1.tv_sec > 0 && t2.tv_sec > 0);
    TEST_CHECK(timercmp(&t1, &t2, <=));
}

void test_release_read_lease()
{
    // another write should be able to proceed if the previous write give up
    // the previous read lease
    init_lease_global();
    uint32_t inum = 0;
    mlfs_time_t t0, t1, t2;
    pid_t client0 = 0, client1 = 1;
    const char *p1 = "/tmp/file1";
    t0 = lease_acquire(p1, mlfs_read_op, T_FILE, client0);
    t1 = lease_acquire(p1, mlfs_write_op, T_FILE, client1);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(t1.tv_sec == -t0.tv_sec && t1.tv_usec == t0.tv_usec);
    t1.tv_sec = -t1.tv_sec;
    lease_release(p1, mlfs_read_op, T_FILE, client0);
    t2 = lease_acquire(p1, mlfs_write_op, T_FILE, client1);
    TEST_CHECK(t1.tv_sec > 0);
    TEST_CHECK(t2.tv_sec > 0);
    TEST_CHECK(t1.tv_sec > 0 && t2.tv_sec > 0);
    TEST_CHECK(timercmp(&t1, &t2, <=));
}

void test_read_after_deletion()
{
    // When the file is deleted by another process, we should notify
    // the client the error
    init_lease_global();
    uint32_t inum = 0;
    mlfs_time_t t0, t1, t2;
    pid_t client0 = 0, client1 = 1;

    const char *p1 = "/tmp/file1";
    t0 = lease_acquire(p1, mlfs_delete_op, T_FILE, client0);
    lease_release(p1, mlfs_delete_op, T_FILE, client0);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    t1 = lease_acquire(p1, mlfs_read_op, T_FILE, client1);
    TEST_CHECK(timercmp(&lease_error, &t1, ==));
}

void test_write_after_deletion()
{
    // write can proceed even if the file is deleted
    init_lease_global();
    uint32_t inum = 0;
    mlfs_time_t t0, t1, t2;
    pid_t client0 = 0, client1 = 1;

    const char *p1 = "/tmp/file1";
    t0 = lease_acquire(p1, mlfs_delete_op, T_FILE, client0);
    lease_release(p1, mlfs_delete_op, T_FILE, client0);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    t1 = lease_acquire(p1, mlfs_write_op, T_FILE, client1);
    TEST_CHECK(timercmp(&t0, &t1, <));
}

void test_double_create()
{
    // two process cannot sequentially create same file without 
    // knowing each other
    init_lease_global();
    mlfs_time_t t0, t1, t2;
    pid_t client0 = 0, client1 = 1;

    const char *p1 = "/tmp/file1";
    t0 = lease_acquire(p1, mlfs_create_op, T_FILE, client0);
    lease_release(p1, mlfs_create_op, T_FILE, client0);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    t1 = lease_acquire(p1, mlfs_create_op, T_FILE, client1);
    TEST_CHECK(timercmp(&t1, &lease_error, ==));
}

void test_double_delete()
{
    // write can proceed even if the path is deleted
    init_lease_global();
    mlfs_time_t t0, t1, t2;
    pid_t client0 = 0, client1 = 1;

    const char *p1 = "/tmp/file1";
    t0 = lease_acquire(p1, mlfs_delete_op, T_FILE, client0);
    lease_release(p1, mlfs_delete_op, T_FILE, client0);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    t1 = lease_acquire(p1, mlfs_delete_op, T_FILE, client1);
    TEST_CHECK(timercmp(&t1, &lease_error, ==));
}

TEST_LIST = {
    {"acquire_lease", test_acquire_lease},
    {"read_lease_contention", test_read_lease_contention},
    {"write_lease_contention", test_write_lease_contention},
    {"release_write_lease", test_release_write_lease},
    {"release_read_lease", test_release_read_lease},
    {"test_read_after_deletion", test_read_after_deletion},
    {"test_write_after_deletion", test_write_after_deletion},
    {"test_double_create", test_double_create},
    {"test_double_delete", test_double_delete},
    {NULL, NULL}};
