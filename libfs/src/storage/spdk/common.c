#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "spdk/nvme.h"
#include "global/mem.h"
#include "global/util.h"

struct ctrlr_entry *g_controllers = NULL;
struct ns_entry *g_namespaces = NULL;
uint8_t g_enable_perf_stats;
spdk_stats_t g_spdk_perf_stats;
float clock_speed_mhz;

// global
long ncpus;
int max_libfs_io_queues;
int max_kernfs_io_queues;

uint32_t Q_DEPTH;

float spdk_get_cpu_clock_speed(void)
{
	FILE* fp;
	char buffer[1024];
	size_t bytes_read;
	char* match;
	float clock_speed;

	/* Read the entire contents of /proc/cpuinfo into the buffer.  */
	fp = fopen ("/proc/cpuinfo", "r");
	bytes_read = fread (buffer, 1, sizeof (buffer), fp);
	fclose (fp);

	/* Bail if read failed or if buffer isn't big enough.  */
	if (bytes_read == 0)
		return 0;

	/* NUL-terminate the text.  */
	buffer[bytes_read] = '\0';

	/* Locate the line that starts with "cpu MHz".  */
	match = strstr(buffer, "cpu MHz");
	if (match == NULL)
		return 0;

	match = strstr(match, ":");

	/* Parse the line to extrace the clock speed.  */
	sscanf (match, ": %f", &clock_speed);
	return clock_speed;
}

long spdk_get_num_cpus(void) {
  return sysconf(_SC_NPROCESSORS_ONLN);
}

unsigned int libspdk_get_n_lbas(void) {
	const struct spdk_nvme_ns_data* nsdata = spdk_nvme_ns_get_data(g_namespaces->ns);
	return nsdata->nsze;
}

unsigned long upper_power_of_two(unsigned int v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

void show_spdk_stats(void)
{
	printf("\n----------------------- spdk statistics\n");
	printf("memcpy   : %.3f ms\n",
			g_spdk_perf_stats.memcpy_tsc / (clock_speed_mhz * 1000.0));
	printf("--------------------------------------\n");
}

static void *libspdk_init_worker(void *arg)
{
	int rc, i, is_kernfs;
	struct spdk_env_opts opts;
  char* kernfs_var;
  pthread_mutexattr_t attr;

	/*
	 * SPDK relies on an abstraction around the local environment
	 * named env that handles memory allocation and PCI device operations.
	 * This library must be initialized first.
	 *
	 */
	spdk_env_opts_init(&opts);
	opts.name = "libspdk";
	opts.shm_id = 10;
	spdk_env_init(&opts);

	clock_speed_mhz = spdk_get_cpu_clock_speed();
  ncpus = spdk_get_num_cpus();

	printf("Initializing NVMe Controllers....\n");

	rc = spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL);
	if (rc) {
		fprintf(stderr, "spdk_nvme_probe() failed\n");
		libspdk_exit();
		return (void*)-1;
	}

  // attach_cb was never called, meaning nothing was found.
  if (!g_namespaces) {
		fprintf(stderr, "spdk_nvme_probe() found no controllers\n");
		libspdk_exit();
		return (void*)-1;
  }

  kernfs_var = getenv("KERNFS");
  is_kernfs = kernfs_var && atoi(kernfs_var) == 1;

  // (iangneal): Limit the number of queues to one per core.
  max_libfs_io_queues = max_libfs_io_queues > ncpus ? ncpus
                                                    : max_libfs_io_queues;
  max_kernfs_io_queues = max_kernfs_io_queues > ncpus ? ncpus
                                                      : max_kernfs_io_queues;

  // (iangneal): set up all qpairs
  g_namespaces->nqpairs = is_kernfs ? max_kernfs_io_queues
                                    : max_libfs_io_queues;
  g_namespaces->qpairs = mlfs_alloc(g_namespaces->nqpairs *
                                    sizeof(*g_namespaces->qpairs));
  g_namespaces->qtexs = mlfs_alloc(g_namespaces->nqpairs *
                                   sizeof(*g_namespaces->qtexs));
  if (!g_namespaces->qpairs || !g_namespaces->qtexs) {
    fprintf(stderr, "mlfs_alloc() failed\n");
    return (void*)-1;
  }

  pthread_mutexattr_init(&attr);
  //pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_DEFAULT);

  for (i = 0; i < g_namespaces->nqpairs; ++i) {
    g_namespaces->qpairs[i] = spdk_nvme_ctrlr_alloc_io_qpair(g_namespaces->ctrlr,
                                                             NULL, 0);
    if (g_namespaces->qpairs[i] == NULL) {
      fprintf(stderr, "ERROR: init_ns_worker_ctx() failed\n");
      return (void*)-1;
    }

    g_namespaces->qtexs[i] = mlfs_alloc(sizeof(*g_namespaces->qtexs[i]));
    if (!g_namespaces->qtexs[i]) {
      perror("mlfs_alloc() for qpair mutex");
      return (void*)-1;
    }
    rc = pthread_mutex_init(g_namespaces->qtexs[i], &attr);
    if (rc) {
      perror("qpair mutex init");
      return (void*)-1;
    }
  }

	g_enable_perf_stats = 1;

	memset(&g_spdk_perf_stats, 0, sizeof(spdk_stats_t));

	return NULL;
}

int libspdk_init(void)
{
	pthread_t thread_id;
  void * ret;

	if (pthread_create(&thread_id, NULL, libspdk_init_worker, NULL)) {
		perror("Fail to create worker thread");
		exit(-1);
	}

	pthread_join(thread_id, &ret);

  if (ret) {
    fprintf(stderr, "libspdk_init() failed.\n");
    return -1;
  }

	return 0;
}

void register_ns(struct spdk_nvme_ctrlr *ctrlr, struct spdk_nvme_ns *ns)
{
	struct ns_entry *entry;
	const struct spdk_nvme_ctrlr_data *cdata;

	/*
	 * spdk_nvme_ctrlr is the logical abstraction in SPDK for an NVMe
	 *  controller.  During initialization, the IDENTIFY data for the
	 *  controller is read using an NVMe admin command, and that data
	 *  can be retrieved using spdk_nvme_ctrlr_get_data() to get
	 *  detailed information on the controller.  Refer to the NVMe
	 *  specification for more details on IDENTIFY for NVMe controllers.
	 */
	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	if (!spdk_nvme_ns_is_active(ns)) {
		printf("Controller %-20.20s (%-20.20s): Skipping inactive NS %u\n",
				cdata->mn, cdata->sn, spdk_nvme_ns_get_id(ns));
		return;
	}

	entry = mlfs_alloc(sizeof(struct ns_entry));
	if (entry == NULL) {
		perror("ns_entry malloc");
		exit(1);
	}

	entry->ctrlr = ctrlr;
	entry->ns = ns;
	entry->next = g_namespaces;
	g_namespaces = entry;

	printf("Namespace ID: %d size: %juGB\n", spdk_nvme_ns_get_id(ns),
			spdk_nvme_ns_get_size(ns) / 1000000000);
}

bool probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
		struct spdk_nvme_ctrlr_opts *opts)
{
	/*
	printf("Attaching to %04x:%02x:%02x.%02x\n",
			spdk_pci_device_get_domain(dev),
			spdk_pci_device_get_bus(dev),
			spdk_pci_device_get_dev(dev),
			spdk_pci_device_get_func(dev));
	*/

	return true;
}

void attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
		struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
	int nsid, num_ns;
	struct ctrlr_entry *entry;
	const struct spdk_nvme_ctrlr_data *cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	entry = mlfs_alloc(sizeof(struct ctrlr_entry));
	if (entry == NULL) {
		perror("ctrlr_entry malloc");
		exit(1);
	}

	/*
	printf("Attached to %04x:%02x:%02x.%02x\n",
			spdk_pci_device_get_domain(dev),
			spdk_pci_device_get_bus(dev),
			spdk_pci_device_get_dev(dev),
			spdk_pci_device_get_func(dev));
	*/

	snprintf(entry->name, sizeof(entry->name), "%-20.20s (%-20.20s)", cdata->mn, cdata->sn);

	entry->ctrlr = ctrlr;
	entry->next = g_controllers;
	g_controllers = entry;

	/*
	 * Each controller has one of more namespaces.  An NVMe namespace is basically
	 *  equivalent to a SCSI LUN.  The controller's IDENTIFY data tells us how
	 *  many namespaces exist on the controller.  For Intel(R) P3X00 controllers,
	 *  it will just be one namespace.
	 *
	 * Note that in NVMe, namespace IDs start at 1, not 0.
	 */
	num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	printf("Using controller %s with %d namespaces.\n", entry->name, num_ns);

	for (nsid = 1; nsid <= num_ns; nsid++) {
		register_ns(ctrlr, spdk_nvme_ctrlr_get_ns(ctrlr, nsid));
	}

  printf("Number of IO queues on device: %d\n", opts->num_io_queues);

  Q_DEPTH = opts->io_queue_size;

  // iangneal: get the maximum number of IO queues supported by our device.
  // Divided by two for kernfs/libfs even split. Leftover queue goes to libfs.
#ifdef CONCURRENT
  max_libfs_io_queues = (opts->num_io_queues / 2) + (opts->num_io_queues % 2);
  max_kernfs_io_queues = (opts->num_io_queues / 2);
#else
  max_libfs_io_queues = 1;
  max_kernfs_io_queues = 1;
#endif
}

void libspdk_exit(void)
{
  int i;
	struct ns_entry *ns_entry = g_namespaces;
	struct ctrlr_entry *ctrlr_entry = g_controllers;


	while (ns_entry) {
		struct ns_entry *next = ns_entry->next;

    for (i = 0; i < ns_entry->nqpairs; ++i) {
		  spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpairs[i]);
      pthread_mutex_destroy(ns_entry->qtexs[i]);
    }

    mlfs_free(ns_entry->qpairs);
		mlfs_free(ns_entry);
		ns_entry = next;
	}

	while (ctrlr_entry) {
		struct ctrlr_entry *next = ctrlr_entry->next;

		spdk_nvme_detach(ctrlr_entry->ctrlr);
		mlfs_free(ctrlr_entry);
		ctrlr_entry = next;
	}

	if (g_enable_perf_stats)
		show_spdk_stats();
}
