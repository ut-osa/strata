/*
 *  Performance test that uses one I/O qpair per thread.
 */

#include <pthread.h>
#include <sys/time.h>

#include "perf_compare.h"

pthread_barrier_t g_barrier;
int ncpus;

static void* run_task(void* arg) {

	struct ns_entry	*ns_entry;
	struct spdk_nvme_qpair *thread_qpair;
	struct arg_sequence	sequence;
  struct spdk_nvme_io_qpair_opts qopts;
	int rc, i;
  int id;

  id = *((int*)arg);
	ns_entry = g_namespaces;

  spdk_nvme_ctrlr_get_default_io_qpair_opts(ns_entry->ctrlr, &qopts,
      sizeof(qopts));

  printf("size: %d, requests: %d\n", qopts.io_queue_size,
      qopts.io_queue_requests);

	while (ns_entry) {
    // Make a unique qpair per thread.
		thread_qpair = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, NULL, 0);
		if (thread_qpair == NULL) {
			printf("ERROR: spdk_nvme_ctrlr_alloc_io_qpair() failed\n");
			return NULL;
		}

		sequence.buf = spdk_dma_zmalloc(BLOCK_SIZE * NUM_BLOCKS, BLOCK_SIZE, NULL);
		sequence.completions = 0;
		sequence.ns_entry = ns_entry;

    // Fill buffer psuedo-randomly.
    for (i = 0; i < BUF_SIZE; ++i) {
      sequence.buf[i] = (char) rand();
    }

    // Wait for everyone.
    pthread_barrier_wait(&g_barrier);

    // Do actual work.
    for (i = 0; i < NUM_BLOCKS; ++i) {
      rc = spdk_nvme_ns_cmd_write(ns_entry->ns, thread_qpair,
                sequence.buf + (i * BLOCK_SIZE),
                (NUM_BLOCKS * id) + i, /* LBA start */
                1, /* number of LBAs */
                write_complete, &sequence, 0);
      if (rc && sequence.completions) {
        // Process I/O
        while (sequence.completions) {
          rc = spdk_nvme_qpair_process_completions(thread_qpair, 0);
          if (rc < 0) {
            fprintf(stderr, "[%d] completion I/O failed (%d, %s), c=%d\n",
                    id, rc, strerror(-1 * rc), sequence.completions);
            exit(rc);
          } else {
            sequence.completions -= rc;
          }
        }
        --i;
        continue;
      } else if (rc) {
        fprintf(stderr, "starting write I/O failed (%d, %s)\n", rc,
            strerror(-1 * rc));
        return (void*) -1;
      } else {
        sequence.completions++;
      }
    }

		while (sequence.completions) {
			rc = spdk_nvme_qpair_process_completions(thread_qpair, 0);
      if (rc < 0) {
        fprintf(stderr, "[%d] completion I/O failed (%d, %s), c=%d\n",
                id, rc, strerror(-1 * rc), sequence.completions);
        exit(rc);
      } else {
        sequence.completions -= rc;
      }
		}

    // Free, go to next if available.
		spdk_nvme_ctrlr_free_io_qpair(thread_qpair);
    spdk_dma_free(sequence.buf);
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
	opts.name = "multithread";
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
