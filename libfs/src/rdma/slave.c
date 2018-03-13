#include <fcntl.h>
#include <sys/stat.h>

#include "common.h"
#include "messages.h"

struct conn_context
{
	char *log;
	struct ibv_mr *log_mr;

	struct message *msg;
	struct ibv_mr *msg_mr;

	int fd;
};

static void send_message(struct rdma_cm_id *id)
{
	struct conn_context *ctx = (struct conn_context *)id->context;

	struct ibv_send_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)id;
	wr.opcode = IBV_WR_SEND;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.send_flags = IBV_SEND_SIGNALED;

	sge.addr = (uintptr_t)ctx->msg;
	sge.length = sizeof(*ctx->msg);
	sge.lkey = ctx->msg_mr->lkey;

	ibv_post_send(id->qp, &wr, &bad_wr);
}

static void post_receive(struct rdma_cm_id *id)
{
	struct ibv_recv_wr wr, *bad_wr = NULL;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)id;
	wr.sg_list = NULL;
	wr.num_sge = 0;

	ibv_post_recv(id->qp, &wr, &bad_wr);
}

static void on_pre_conn(struct rdma_cm_id *id)
{
	struct conn_context *ctx = (struct conn_context *)malloc(sizeof(struct conn_context));

	id->context = ctx;
	
	posix_memalign((void **)&ctx->log, sysconf(_SC_PAGESIZE), LOG_SIZE);
	ctx->log_mr = ibv_reg_mr(rc_get_pd(), ctx->log, LOG_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

	posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
	ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

	post_receive(id);
}

static void on_connection(struct rdma_cm_id *id)
{
	struct conn_context *ctx = (struct conn_context *)id->context;

	ctx->msg->id = MSG_MR;
	ctx->msg->data.mr.addr = (uintptr_t)ctx->log_mr->addr;
	ctx->msg->data.mr.rkey = ctx->log_mr->rkey;

	send_message(id);
}

static void on_completion(struct ibv_wc *wc)
{
	struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
	struct conn_context *ctx = (struct conn_context *)id->context;

	if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
		uint32_t size = ntohl(wc->imm_data);

		if (size == 0) {
			ctx->msg->id = MSG_DONE;
			send_message(id);

			// don't need post_receive() since we're done with this connection

		}
		else {
			printf("received %i bytes.\n", size);
			post_receive(id);

			ctx->msg->id = MSG_READY;
			send_message(id);
		}
	}
}

static void on_disconnect(struct rdma_cm_id *id)
{
	struct conn_context *ctx = (struct conn_context *)id->context;

	close(ctx->fd);

	ibv_dereg_mr(ctx->log_mr);
	ibv_dereg_mr(ctx->msg_mr);

	free(ctx->log);
	free(ctx->msg);

	printf("Finish transferring application log(s)");

	free(ctx);
}

void* rc_slave_loop(void *port)
{
	struct sockaddr_in6 addr;
	struct rdma_cm_id *listener = NULL;
	struct rdma_event_channel *ec = NULL;

	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(atoi(port));

	ec = rdma_create_event_channel();
	rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP);
	rdma_bind_addr(listener, (struct sockaddr *)&addr);
	rdma_listen(listener, 10); /* backlog=10 is arbitrary */

	event_loop(ec, 0); // don't exit on disconnect

	rdma_destroy_id(listener);
	rdma_destroy_event_channel(ec);

	return 0;
}

int main(int argc, char **argv)
{
	//pthread_t server_thread;
	rc_init(
	  on_pre_conn,
		on_connection,
		on_completion,
		on_disconnect);

	printf("waiting for connections. interrupt (^C) to exit.\n");

	rc_slave_loop((void*)DEFAULT_PORT);

	return 0;
}
