#include <fcntl.h>
#include <libgen.h>

#include "common.h"
#include "messages.h"

//size of rdma write - only used for testing purposes when running this module as main 
uint64_t write_size;

struct peer_context
{
	//start address for peer's log
	uint64_t start_addr;
	
	//size of log data sync'd with remote peer
	uint64_t synced_len;
	
	//peer's rdma key for specified mr
	uint32_t rkey;
};

struct master_context
{
	char *log;
	struct ibv_mr *log_mr;
	uint64_t log_len;
	
	struct message *msg;
	struct ibv_mr *msg_mr;

	//TODO: to support multiple replicas, change this to an array of peer contexts
	struct peer_context peer_ctx;
};

//issue a remote rdma write
static void write_remote(struct rdma_cm_id *id, uint64_t local_addr, uint64_t remote_addr, uint64_t len)
{
	//FIXME: need to emulate NVM latency before issuing a remote write
	//TODO: emulating NVM BW is still an open question

	struct master_context *ctx = (struct master_context *)id->context;
	struct ibv_send_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)id;
	wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.imm_data = htonl(len);
	wr.wr.rdma.remote_addr = remote_addr;
	wr.wr.rdma.rkey = ctx->peer_ctx.rkey;

	if (len) {
		wr.sg_list = &sge;
		wr.num_sge = 1;

		//sge.addr = (uintptr_t)ctx->log;
		sge.addr = local_addr;
		sge.length = len;
		sge.lkey = ctx->log_mr->lkey;
	}
	ibv_post_send(id->qp, &wr, &bad_wr);
}

//call this function to sync log data to replica (based on specified id)
static void sync_remote(struct rdma_cm_id *id)
{
	struct master_context *ctx = (struct master_context *)id->context;
	uint64_t local_addr = (uintptr_t)ctx->log + ctx->peer_ctx.synced_len;
	uint64_t remote_addr = (uintptr_t)ctx->peer_ctx.start_addr + ctx->peer_ctx.synced_len;
	uint64_t len = ctx->log_len - ctx->peer_ctx.synced_len;
	write_remote(id, local_addr, remote_addr, len);
	
	//update remote replica sync'd data info
	ctx->peer_ctx.synced_len = ctx->peer_ctx.synced_len + len;
}

static void post_receive(struct rdma_cm_id *id)
{
	struct master_context *ctx = (struct master_context *)id->context;

	struct ibv_recv_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)id;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	sge.addr = (uintptr_t)ctx->msg;
	sge.length = sizeof(*ctx->msg);
	sge.lkey = ctx->msg_mr->lkey;

	ibv_post_recv(id->qp, &wr, &bad_wr);
}

static void on_pre_conn(struct rdma_cm_id *id)
{
	struct master_context *ctx = (struct master_context *)id->context;

	posix_memalign((void **)&ctx->log, sysconf(_SC_PAGESIZE), LOG_SIZE);
	ctx->log_mr = ibv_reg_mr(rc_get_pd(), ctx->log, LOG_SIZE, IBV_ACCESS_LOCAL_WRITE);

	posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
	ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

	post_receive(id);
}

static void on_completion(struct ibv_wc *wc)
{
	struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc->wr_id);
	struct master_context *ctx = (struct master_context *)id->context;
	if (wc->opcode & IBV_WC_RECV) {
		//printf("task received %i.\n", ctx->msg->id);
		if (ctx->msg->id == MSG_MR) {
			ctx->peer_ctx.start_addr = ctx->msg->data.mr.addr;
			ctx->peer_ctx.rkey = ctx->msg->data.mr.rkey;
			printf("received MR, sending application log(s)\n");
			//FIXME this is only done for testing, comment out when integrating to strata
			write_remote(id, (uintptr_t)ctx->log, ctx->peer_ctx.start_addr, write_size);
		}
		else if (ctx->msg->id == MSG_READY) {
		//send empty msg to end connection once replica sends us receipt notification
		//FIXME: this is just for testing purposes, comment out when integrating to strata
		printf("received READY, sending empty string to end connection\n");
		write_remote(id, (uintptr_t)ctx->log, ctx->peer_ctx.start_addr, 0);
		}
		else if (ctx->msg->id == MSG_DONE) {
			printf("received DONE, disconnecting\n");
			rc_disconnect(id);
			return;
		}

		post_receive(id);
	}
}

void rc_master_loop(const char *host, const char *port, void *context)
{
	struct addrinfo *addr;
	struct rdma_cm_id *conn = NULL;
	struct rdma_event_channel *ec = NULL;
	struct rdma_conn_param cm_params;

	getaddrinfo(host, port, NULL, &addr);

	ec = rdma_create_event_channel();
	rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP);
	rdma_resolve_addr(conn, NULL, addr->ai_addr, TIMEOUT_IN_MS);

	freeaddrinfo(addr);

	conn->context = context;

	build_params(&cm_params);

	event_loop(ec, 1); // exit on disconnect

	rdma_destroy_event_channel(ec);
}

int main(int argc, char **argv)
{
	struct master_context ctx;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <server-address> <write-size> \n", argv[0]);
		return 1;
	}

	write_size = atoi(argv[2]);
	rc_init(
	  on_pre_conn,
		NULL,
		 // on connect
		on_completion,
		NULL); // on disconnect

	  rc_master_loop(argv[1], DEFAULT_PORT, &ctx);

	return 0;
}
