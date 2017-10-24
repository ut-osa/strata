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

	printf("\n");
	printf("----------------------- spdk statistics\n");
	printf("memcpy   : %.3f ms\n", 
			g_spdk_perf_stats.memcpy_tsc / (clock_speed_mhz * 1000.0));
	printf("--------------------------------------\n");
}

static void *libspdk_init_worker(void *arg) 
{
	int rc;
	struct spdk_env_opts opts;

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

	printf("Initializing NVMe Controllers....\n");

	rc = spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL);
	if (rc != 0) {
		fprintf(stderr, "spdk_nvme_probe() failed\n");
		libspdk_exit();
		return NULL;
	}

	g_namespaces->qpair = spdk_nvme_ctrlr_alloc_io_qpair(g_namespaces->ctrlr, NULL, 0);
	if (g_namespaces->qpair == NULL) {
		printf("ERROR: init_ns_worker_ctx() failed\n");
		return NULL;
	}

	g_enable_perf_stats = 1;

	memset(&g_spdk_perf_stats, 0, sizeof(spdk_stats_t));

	return NULL;
}

int libspdk_init(void) 
{
	pthread_t thread_id;

	if (pthread_create(&thread_id, NULL, libspdk_init_worker, NULL)) {
		perror("Fail to create worker thread");
		exit(-1);
	}

	pthread_join(thread_id, NULL);

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
}

void libspdk_exit(void) 
{
	struct ns_entry *ns_entry = g_namespaces;
	struct ctrlr_entry *ctrlr_entry = g_controllers;

	while (ns_entry) {
		struct ns_entry *next = ns_entry->next;

		spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpair);

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
