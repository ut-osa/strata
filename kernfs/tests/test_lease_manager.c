#include "acutest.h"
#include "lease_manager.h"
#include <sys/time.h>

void test_acquire_lease()
{
    init_lease_interval();
    uint32_t inum0 = 0, inum1 = 1;
    mlfs_time_t t0, t1, current_time;
    t0 = acquire_read_lease(inum0);
    gettimeofday(&current_time, NULL);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(timercmp(&t0, &current_time, >));
    t1 = acquire_write_lease(inum1);
    TEST_CHECK(t1.tv_sec > 0 && t1.tv_usec > 0);
    TEST_CHECK(timercmp(&t1, &current_time, >));
}

void test_read_lease_contention()
{
    init_lease_interval();
    uint32_t inum = 0;
    mlfs_time_t t0, t1, t2;
    t0 = acquire_read_lease(inum);
    t1 = acquire_read_lease(inum);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(timercmp(&t0, &t1, <=));
    t2 = acquire_write_lease(inum);
    TEST_CHECK(t2.tv_sec == -t1.tv_sec && t2.tv_usec == t1.tv_usec);
}

void test_write_lease_contention()
{
    init_lease_interval();
    uint32_t inum = 0;
    mlfs_time_t t0, t1, t2;
    t0 = acquire_write_lease(inum);
    t1 = acquire_write_lease(inum);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(t1.tv_sec < 0);
    TEST_CHECK(t1.tv_sec == -t0.tv_sec && t1.tv_usec == t0.tv_usec);
}

void test_release_write_lease()
{
    init_lease_interval();
    uint32_t inum = 0;
    mlfs_time_t t0, t1, t2;
    t0 = acquire_write_lease(inum);
    t1 = acquire_write_lease(inum);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(t1.tv_sec == -t0.tv_sec && t1.tv_usec == t0.tv_usec);
    t1.tv_sec = -t1.tv_sec;
    release_write_lease(inum);
    t2 = acquire_write_lease(inum);
    TEST_CHECK(t1.tv_sec > 0);
    TEST_CHECK(t2.tv_sec > 0);
    TEST_CHECK(t1.tv_sec > 0 && t2.tv_sec > 0);
    TEST_CHECK(timercmp(&t1, &t2, <=));
}

void test_release_read_lease()
{
    init_lease_interval();
    uint32_t inum = 0;
    mlfs_time_t t0, t1, t2;
    t0 = acquire_read_lease(inum);
    t1 = acquire_write_lease(inum);
    TEST_CHECK(t0.tv_sec > 0 && t0.tv_usec > 0);
    TEST_CHECK(t1.tv_sec == -t0.tv_sec && t1.tv_usec == t0.tv_usec);
    t1.tv_sec = -t1.tv_sec;
    release_read_lease(inum);
    t2 = acquire_write_lease(inum);
    TEST_CHECK(t1.tv_sec > 0);
    TEST_CHECK(t2.tv_sec > 0);
    TEST_CHECK(t1.tv_sec > 0 && t2.tv_sec > 0);
    TEST_CHECK(timercmp(&t1, &t2, <=));
}

TEST_LIST = {
    { "acquire_lease", test_acquire_lease },
    { "read_lease_contention", test_read_lease_contention },
    { "write_lease_contention", test_write_lease_contention },
    { "release_write_lease", test_release_write_lease },
    { "release_read_lease", test_release_read_lease },
    { NULL, NULL }
};
