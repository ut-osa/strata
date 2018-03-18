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

#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>

extern const int TIMEOUT_IN_MS;
extern const uint64_t LOG_SIZE;

typedef void(*pre_conn_cb_fn)(struct rdma_cm_id *id);
typedef void(*connect_cb_fn)(struct rdma_cm_id *id);
typedef void(*completion_cb_fn)(struct ibv_wc *wc);
typedef void(*disconnect_cb_fn)(struct rdma_cm_id *id);

void build_context(struct ibv_context *verbs);
void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
void build_params(struct rdma_conn_param *params);
void event_loop(struct rdma_event_channel *ec, int exit_on_disconnect);
void * poll_cq(void *);
void rc_init(pre_conn_cb_fn, connect_cb_fn, completion_cb_fn, disconnect_cb_fn);
void rc_disconnect(struct rdma_cm_id *id);
void rc_die(const char *message);
struct ibv_pd * rc_get_pd();

#endif
