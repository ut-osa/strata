/*The MIT License

Copyright (c) 2011 Dominic Tarr

Permission is hereby granted, free of charge, 
	   to any person obtaining a copy of this software and 
	   associated documentation files (the "Software"), to 
	   deal in the Software without restriction, including 
	   without limitation the rights to use, copy, modify, 
	   merge, publish, distribute, sublicense, and/or sell 
	   copies of the Software, and to permit persons to whom 
	   the Software is furnished to do so, 
	   subject to the following conditions:

	   The above copyright notice and this permission notice 
	   shall be included in all copies or substantial portions of the Software.

	   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
	   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
	   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
	   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR 
	   ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
	   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
	   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.*/


#include "common.h"

struct context {
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_comp_channel *comp_channel;

	pthread_t cq_poller_thread;
};

static struct context *s_ctx = NULL;
static pre_conn_cb_fn s_on_pre_conn_cb = NULL;
static connect_cb_fn s_on_connect_cb = NULL;
static completion_cb_fn s_on_completion_cb = NULL;
static disconnect_cb_fn s_on_disconnect_cb = NULL;

const int TIMEOUT_IN_MS = 500;
const uint64_t LOG_SIZE = 2 * 1024 * 1024;

void build_connection(struct rdma_cm_id *id)
{
	struct ibv_qp_init_attr qp_attr;

	build_context(id->verbs);
	build_qp_attr(&qp_attr);

	rdma_create_qp(id, s_ctx->pd, &qp_attr);
}

void build_context(struct ibv_context *verbs)
{
	if (s_ctx) {
		if (s_ctx->ctx != verbs)
			rc_die("cannot handle events in more than one context.");

		return;
	}

	s_ctx = (struct context *)malloc(sizeof(struct context));

	s_ctx->ctx = verbs;

	s_ctx->pd = ibv_alloc_pd(s_ctx->ctx);
	s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx);
	s_ctx->cq = ibv_create_cq(s_ctx->ctx, 10, NULL, s_ctx->comp_channel, 0); /* cqe=10 is arbitrary */
	ibv_req_notify_cq(s_ctx->cq, 0);

	pthread_create(&s_ctx->cq_poller_thread, NULL, poll_cq, NULL);
}

void build_params(struct rdma_conn_param *params)
{
	memset(params, 0, sizeof(*params));

	params->initiator_depth = params->responder_resources = 1;
	params->rnr_retry_count = 7; /* infinite retry */
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr)
{
	memset(qp_attr, 0, sizeof(*qp_attr));

	qp_attr->send_cq = s_ctx->cq;
	qp_attr->recv_cq = s_ctx->cq;
	qp_attr->qp_type = IBV_QPT_RC;

	qp_attr->cap.max_send_wr = 10;
	qp_attr->cap.max_recv_wr = 10;
	qp_attr->cap.max_send_sge = 1;
	qp_attr->cap.max_recv_sge = 1;
}

void event_loop(struct rdma_event_channel *ec, int exit_on_disconnect)
{
	struct rdma_cm_event *event = NULL;
	struct rdma_conn_param cm_params;

	build_params(&cm_params);

	while (rdma_get_cm_event(ec, &event) == 0) {
		struct rdma_cm_event event_copy;

		memcpy(&event_copy, event, sizeof(*event));
		rdma_ack_cm_event(event);

		if (event_copy.event == RDMA_CM_EVENT_ADDR_RESOLVED) {
			build_connection(event_copy.id);

			if (s_on_pre_conn_cb)
				s_on_pre_conn_cb(event_copy.id);

			rdma_resolve_route(event_copy.id, TIMEOUT_IN_MS);

		}
		else if (event_copy.event == RDMA_CM_EVENT_ROUTE_RESOLVED) {
			rdma_connect(event_copy.id, &cm_params);

		}
		else if (event_copy.event == RDMA_CM_EVENT_CONNECT_REQUEST) {
			build_connection(event_copy.id);

			if (s_on_pre_conn_cb)
				s_on_pre_conn_cb(event_copy.id);

			rdma_accept(event_copy.id, &cm_params);

		}
		else if (event_copy.event == RDMA_CM_EVENT_ESTABLISHED) {
			if (s_on_connect_cb)
				s_on_connect_cb(event_copy.id);

		}
		else if (event_copy.event == RDMA_CM_EVENT_DISCONNECTED) {
			rdma_destroy_qp(event_copy.id);

			if (s_on_disconnect_cb)
				s_on_disconnect_cb(event_copy.id);

			rdma_destroy_id(event_copy.id);

			if (exit_on_disconnect)
				break;

		}
		else {
			char out[19];
			sprintf(out, "Unknown event %d \n", event_copy.event);
			rc_die(out);
		}
	}
}

void * poll_cq(void *ctx)
{
	struct ibv_cq *cq;
	struct ibv_wc wc;

	while (1) {
		ibv_get_cq_event(s_ctx->comp_channel, &cq, &ctx);
		ibv_ack_cq_events(cq, 1);
		ibv_req_notify_cq(cq, 0);

		while (ibv_poll_cq(cq, 1, &wc)) {
			if (wc.status == IBV_WC_SUCCESS)
				s_on_completion_cb(&wc);
			else
				rc_die("poll_cq: status is not IBV_WC_SUCCESS");
		}
	}

	return NULL;
}

void rc_init(pre_conn_cb_fn pc, connect_cb_fn conn, completion_cb_fn comp, disconnect_cb_fn disc)
{
	s_on_pre_conn_cb = pc;
	s_on_connect_cb = conn;
	s_on_completion_cb = comp;
	s_on_disconnect_cb = disc;
}

void rc_disconnect(struct rdma_cm_id *id)
{
	rdma_disconnect(id);
}

void rc_die(const char *reason)
{
	fprintf(stderr, "%s\n", reason);
	exit(EXIT_FAILURE);
}

struct ibv_pd * rc_get_pd()
{
	return s_ctx->pd;
}
