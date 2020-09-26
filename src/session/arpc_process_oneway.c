/*
 * Copyright(C) 2020 Ruijie Network. All rights reserved.
 */

/*!
* \file xxx.x
* \brief xxx
* 
* 包含..
*
* \copyright 2020 Ruijie Network. All rights reserved.
* \author hongchunhua@ruijie.com.cn
* \version v1.0.0
* \date 2020.08.05
* \note none 
*/

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>

#include "arpc_com.h"
#include "threadpool.h"
#include "arpc_request.h"

static int oneway_msg_async_deal(void *usr_ctx);

int process_oneway_header(struct xio_msg *msg, struct oneway_ops *ops, uint64_t iov_max_len, void *usr_ctx)
{
	struct proc_header_func head_ops;
	head_ops.alloc_cb = ops->alloc_cb;
	head_ops.free_cb = ops->free_cb;
	head_ops.proc_head_cb = ops->proc_head_cb;
	return create_xio_msg_usr_buf(msg, &head_ops, iov_max_len, usr_ctx);
}

int process_oneway_data(struct xio_msg *req, struct oneway_ops *ops, int last_in_rxq, void *usr_ctx)
{
	struct xio_iovec			*sglist = vmsg_base_sglist(&req->in);
	uint32_t					nents = vmsg_sglist_nents(&req->in);
	uint32_t					i;
	int							ret;
	struct arpc_vmsg 			rev_iov;
	uint32_t 				 	flags = 0;
	struct arpc_thread_param 	*async_param;

	LOG_THEN_RETURN_VAL_IF_TRUE((!req), ARPC_ERROR, "req null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!ops), ARPC_ERROR, "ops null.");
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(IS_SET(req->usr_flags, FLAG_MSG_ERROR_DISCARD_DATA), release_req, "dicard data.");
	memset(&rev_iov, 0, sizeof(struct arpc_vmsg));
	rev_iov.head = req->in.header.iov_base;
	rev_iov.head_len = req->in.header.iov_len;
	rev_iov.vec_num = nents;
	rev_iov.total_data = req->in.total_data_len;
	rev_iov.vec = NULL;
	if (IS_SET(req->usr_flags, METHOD_ALLOC_DATA_BUF) && nents) {
		rev_iov.vec = (struct arpc_iov *)vmsg_base_sglist(&req->in);
	}
	if (IS_SET(req->usr_flags, METHOD_ARPC_PROC_SYNC)) {
		ARPC_LOG_DEBUG("set proc_data_cb data.");
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ops->proc_data_cb, free_data, "proc_data_cb is null.");
		ret = ops->proc_data_cb(&rev_iov, &flags, usr_ctx);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_data, "proc_data_cb  return fail.");
		req->usr_flags |= flags;
		goto free_data;
	}else {
		ARPC_LOG_DEBUG("set proc_async_cb data.");
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!IS_SET(req->usr_flags, METHOD_ALLOC_DATA_BUF) && nents, 
										free_data, "system alloc memory do not allowe deal on async, nents[%u].", nents);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ops->free_cb, free_data, "free_cb is null, can't do async.");
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ops->proc_async_cb, free_data, "proc_async_cb is null, can't do async.");

		async_param = (struct arpc_thread_param * )ARPC_MEM_ALLOC(sizeof(struct arpc_thread_param), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!async_param, free_data, "async_param is null, can't do async.");

		memset(async_param, 0, sizeof(struct arpc_thread_param));
		async_param->ops.alloc_cb = ops->alloc_cb;
		async_param->ops.free_cb = ops->free_cb;
		async_param->ops.proc_async_cb = NULL;
		async_param->ops.release_rsp_cb = NULL;
		async_param->ops.proc_oneway_async_cb = ops->proc_async_cb;
		async_param->rsp_ctx = NULL;
		async_param->req_msg = req;
		async_param->rev_iov = rev_iov;
		async_param->usr_ctx = usr_ctx;
		async_param->loop = oneway_msg_async_deal;

		//deepcopy
		if(rev_iov.head_len){
			async_param->rev_iov.head = ARPC_MEM_ALLOC(rev_iov.head_len, NULL);
			memcpy(async_param->rev_iov.head, rev_iov.head, rev_iov.head_len);
		}
		ret = post_to_async_thread(async_param);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_data, "post_to_async_thread fail.");
	}
	return 0;
free_data:
	destroy_xio_msg_usr_buf(req, ops->free_cb, usr_ctx);
release_req:
	xio_release_msg(req);
	return -1;
}
static int oneway_msg_async_deal(void *usr_ctx)
{
	struct arpc_thread_param *async = (struct arpc_thread_param *)usr_ctx;
	int ret;
	uint32_t flags =0;

	ARPC_LOG_DEBUG("Note: msg deal on thread[%lu]...", pthread_self());// to do
	LOG_THEN_RETURN_VAL_IF_TRUE(!async, ARPC_ERROR, "async null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!async->ops.proc_oneway_async_cb, ARPC_ERROR, "proc_async_cb null.");

	ret = async->ops.proc_oneway_async_cb(&async->rev_iov, &flags, async->usr_ctx);
	LOG_ERROR_IF_VAL_TRUE(ret, "proc_oneway_async_cb error.");
	if (async->req_msg) {
		if (!IS_SET(flags, METHOD_CALLER_HIJACK_RX_DATA)){
			destroy_xio_msg_usr_buf(async->req_msg, async->ops.free_cb, async->usr_ctx);
		}
		xio_release_msg(async->req_msg);
	}

	// free
	SAFE_FREE_MEM(async->rev_iov.head);
	SAFE_FREE_MEM(async);

	return 0;
}
