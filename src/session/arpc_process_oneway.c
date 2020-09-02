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


int _process_oneway_header(struct xio_msg *msg, struct oneway_ops *ops, uint64_t iov_max_len, void *usr_ctx)
{
	struct _proc_header_func head_ops;
	head_ops.alloc_cb = ops->alloc_cb;
	head_ops.free_cb = ops->free_cb;
	head_ops.proc_head_cb = ops->proc_head_cb;
	return _create_header_source(msg, &head_ops, iov_max_len, usr_ctx);
}

int _process_oneway_data(struct xio_msg *req,
			   struct oneway_ops *ops,
		       int last_in_rxq,
		       void *usr_ctx)
{
	struct xio_iovec	*sglist = vmsg_base_sglist(&req->in);
	uint32_t			nents = vmsg_sglist_nents(&req->in);
	uint32_t			i;
	int				ret;
	struct arpc_vmsg 	rev_iov;
	struct _async_proc_ops async_ops;
	uint32_t flags = 0;

	LOG_THEN_RETURN_VAL_IF_TRUE((!req), ARPC_ERROR, "req null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!ops), ARPC_ERROR, "ops null.");
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(IS_SET(req->usr_flags, FLAG_MSG_ERROR_DISCARD_DATA), 
									release_req, "dicard data.");

	memset(&rev_iov, 0, sizeof(struct arpc_vmsg));
	rev_iov.head = req->in.header.iov_base;
	rev_iov.head_len = req->in.header.iov_len;
	rev_iov.vec_num = nents;
	rev_iov.total_data = req->in.total_data_len;
	
	if (IS_SET(req->usr_flags, METHOD_ALLOC_DATA_BUF)) {
		rev_iov.vec = (struct arpc_iov *)vmsg_base_sglist(&req->in);
	}else{
		// todo
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(1, release_req, "it need to do more, fix me.");
	}

	if (IS_SET(req->usr_flags, METHOD_ARPC_PROC_SYNC)) {
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ops->proc_data_cb, free_data, "proc_data_cb is null.");
		ret = ops->proc_data_cb(&rev_iov, &flags, usr_ctx);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_data, "proc_data_cb  return fail.");
		req->usr_flags |= flags;
		goto free_data;
	}else {
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!IS_SET(req->usr_flags, METHOD_ALLOC_DATA_BUF), 
										free_data, "system alloc memory do not allowe deal on async.");
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ops->free_cb, free_data, "free_cb is null, can't do async.");
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ops->proc_async_cb, free_data, "proc_async_cb is null, can't do async.");
		async_ops.alloc_cb = ops->alloc_cb;
		async_ops.free_cb = ops->free_cb;
		async_ops.proc_oneway_async_cb = ops->proc_async_cb;
		async_ops.proc_async_cb = NULL;
		ret = _post_iov_to_async_thread(&rev_iov, req, &async_ops, usr_ctx);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_data, "_post_iov_to_async_thread fail.");
	}
	
	return 0;

free_data:
	_clean_header_source(req, ops->free_cb, usr_ctx);
release_req:
	xio_release_msg(req);
	return -1;
}
