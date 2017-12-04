/*
 * Measures performance of writes across multiple threads to a single qpair.
 */
#define _GNU_SOURCE

#include <pthread.h>
#include <sys/time.h>

#include "perf_compare.h"

pthread_barrier_t g_barrier;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
int ncpus;
struct arg_sequence	sequence;

static void* run_task(void* arg) {

	struct ns_entry	*ns_entry;
	struct spdk_nvme_qpair *thread_qpair;
  struct spdk_nvme_io_qpair_opts qopts;
	int rc, i;
  int id;

  id = *((int*)arg);
	ns_entry = g_namespaces;

	while (ns_entry) {
    // Make the qpair.
    pthread_mutex_lock(&g_mutex);
    if (!ns_entry->qpair) {
      // Setup qpair
      spdk_nvme_ctrlr_get_default_io_qpair_opts(ns_entry->ctrlr, &qopts,
          sizeof(qopts));
      qopts.io_queue_requests = qopts.io_queue_size * 2 * ncpus;
      ns_entry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr,
          &qopts, sizeof(qopts));
      if (ns_entry->qpair == NULL) {
        printf("ERROR: spdk_nvme_ctrlr_alloc_io_qpair() failed\n");
        exit(-1);
      }
      // setup data
      sequence.buf = spdk_dma_zmalloc(BLOCK_SIZE * NUM_BLOCKS, BLOCK_SIZE, NULL);
      sequence.completions = 0;
      sequence.ns_entry = ns_entry;
      // Fill buffer psuedo-randomly.
      for (i = 0; i < BUF_SIZE; ++i) {
        sequence.buf[i] = (char) rand();
      }
    }
    pthread_mutex_unlock(&g_mutex);

    // Wait for everyone.
    pthread_barrier_wait(&g_barrier);

    // Do actual work.
    for (i = 0; i < NUM_BLOCKS; ++i) {
      pthread_mutex_lock(&g_mutex);
      rc = spdk_nvme_ns_cmd_write(ns_entry->ns, ns_entry->qpair,
                sequence.buf + (i * BLOCK_SIZE),
                (NUM_BLOCKS * id) + i, /* LBA start */
                1, /* number of LBAs */
                write_complete, &sequence, 0);
      if (rc && sequence.completions) {
        // Process I/O
        while (sequence.completions) {
          rc = spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
          if (rc < 0) {
            fprintf(stderr, "[%d] completion I/O failed (%d, %s), c=%d\n",
                    id, rc, strerror(-1 * rc), sequence.completions);
            exit(rc);
          } else {
            sequence.completions -= rc;
          }
        }
        --i; goto end_loop;
      } else if (rc) {
        fprintf(stderr,
            "[%d] starting write I/O failed at %d/%d (%d, %s), c=%d\n",
            id, i, NUM_BLOCKS, rc, strerror(-1 * rc), sequence.completions);
        exit(-1);
      } else {
        sequence.completions++;
      }

end_loop:
      pthread_mutex_unlock(&g_mutex);
      pthread_yield();
    }

    pthread_mutex_lock(&g_mutex);
		while (sequence.completions) {
			rc = spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
      if (rc < 0) {
        fprintf(stderr, "[%d] completion I/O failed (%d, %s), c=%d\n",
                id, rc, strerror(-1 * rc), sequence.completions);
      } else {
        sequence.completions -= rc;
      }
		}
    pthread_mutex_unlock(&g_mutex);

    // Free, go to next if available.
    pthread_mutex_lock(&g_mutex);
    if (ns_entry->qpair) {
      spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpair);
      spdk_dma_free(sequence.buf);
    }
    pthread_mutex_unlock(&g_mutex);

		ns_entry = ns_entry->next;
	}

  return NULL;
}


int main(int argc, char **argv) {

	int rc, i;
  struct timeval stop, start;
	struct spdk_env_opts opts;
  pthread_t *threads;
  int *thread_ids;

  // Regular initialization.
  srand(time(NULL));
  ncpus = get_cpus();
  pthread_barrier_init(&g_barrier, NULL, ncpus);

  threads = malloc(sizeof(*threads) * ncpus);
  if (!threads) {
    fprintf(stderr, "malloc failed.\n");
    rc = -1; goto finish;
  }

  thread_ids = malloc(sizeof(*thread_ids) * ncpus);
  if (!thread_ids) {
    fprintf(stderr, "malloc failed.\n");
    rc = -1; goto finish;
  }

	/*
	 * SPDK relies on an abstraction around the local environment
	 * named env that handles memory allocation and PCI device operations.
	 * This library must be initialized first.
	 *
	 */
	spdk_env_opts_init(&opts);
	opts.name = "deepqueue";
	opts.shm_id = 0;
	spdk_env_init(&opts);

	printf("Initializing NVMe Controllers\n");

	/*
	 * Start the SPDK NVMe enumeration process.  probe_cb will be called
	 *  for each NVMe controller found, giving our application a choice on
	 *  whether to attach to each controller.  attach_cb will then be
	 *  called for each controller after the SPDK NVMe driver has completed
	 *  initializing the controller we chose to attach.
	 */
	rc = spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL);
	if (rc != 0) {
		fprintf(stderr, "spdk_nvme_probe() failed\n");
    goto finish;
	}

	if (g_controllers == NULL) {
		fprintf(stderr, "no NVMe controllers found\n");
    goto finish;
	}

	printf("Initialization complete.\n");

  // Start up some pthreads.
  gettimeofday(&start, NULL);
  for (i = 0; i < ncpus; ++i) {
    thread_ids[i] = i;
    rc = pthread_create(&threads[i], NULL, run_task, &thread_ids[i]);
    if (rc) {
      fprintf(stderr, "Could not create pthread.\n");
      goto finish;
    }
  }

  // Wait for finish.
  for (i = 0; i < ncpus; ++i) {
    void* ret;
    rc = pthread_join(threads[i], &ret);
    if (rc) {
      fprintf(stderr, "Could not join pthread.\n");
      goto finish;
    }
    if (ret) {
      fprintf(stderr, "Thread %d failed.\n", i);
    }
  }
  gettimeofday(&stop, NULL);

  printf("Test complete in %lu microseconds.\n", stop.tv_usec - start.tv_usec);

finish:
	cleanup();
	return rc;
}
