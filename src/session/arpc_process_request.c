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


int _process_request_header(struct xio_msg *msg, struct request_ops *ops, uint64_t iov_max_len, void *usr_ctx)
{
	struct _proc_header_func head_ops;
	head_ops.alloc_cb = ops->alloc_cb;
	head_ops.free_cb = ops->free_cb;
	head_ops.proc_head_cb = ops->proc_head_cb;
	return _create_header_source(msg, &head_ops, iov_max_len, usr_ctx);
}

int _process_request_data(struct xio_msg *req,
			   struct request_ops *ops,
		       int last_in_rxq,
		       void *usr_ctx)
{
	struct xio_iovec	*sglist = vmsg_base_sglist(&req->in);
	uint32_t			nents = vmsg_sglist_nents(&req->in);
	uint32_t			i;
	struct arpc_vmsg 	rev_iov;
	struct arpc_rsp 	rsp;
	struct _async_proc_ops async_ops;
	int				ret;

	LOG_THEN_RETURN_VAL_IF_TRUE((!req), ARPC_ERROR, "req null.");

	memset(&rev_iov, 0, sizeof(struct arpc_vmsg));
	rev_iov.head = req->in.header.iov_base;
	rev_iov.head_len = req->in.header.iov_len;
	rev_iov.vec_num = nents;
	rev_iov.vec = (struct arpc_iov *)sglist;
	rev_iov.total_data = req->in.total_data_len;
	
	// 数据处理,并获得回复消息
	memset(&rsp, 0, sizeof(struct arpc_rsp));
	rsp.rsp_fd = (void *)req;
	if(IS_SET(req->usr_flags, METHOD_ARPC_PROC_SYNC) && ops->proc_data_cb){
		ret = ops->proc_data_cb(&rev_iov, &rsp, usr_ctx);
		LOG_ERROR_IF_VAL_TRUE(ret, "proc_data_cb that define for user is error.");
		if (!IS_SET(rsp.flags, METHOD_CALLER_HIJACK_RX_DATA)) {
			_clean_header_source(req, ops->free_cb, usr_ctx);
		}
		if (IS_SET(rsp.flags, METHOD_CALLER_ASYNC)) {
			goto end;
		}else{
			goto do_respone;
		}
	}else if(ops->free_cb && ops->proc_async_cb && ops->release_rsp_cb){
		if (!IS_SET(req->usr_flags, METHOD_ALLOC_DATA_BUF) && nents) {
			ARPC_LOG_ERROR("caller don't alloc buf to rx data, can't proc async fail.");
			goto free_user_buf;
		}
		async_ops.alloc_cb = ops->alloc_cb;
		async_ops.free_cb = ops->free_cb;
		async_ops.proc_async_cb = ops->proc_async_cb;
		async_ops.release_rsp_cb = ops->release_rsp_cb;
		async_ops.proc_oneway_async_cb = NULL;
		ret = _post_iov_to_async_thread(&rev_iov, req, &async_ops, usr_ctx);
		if(ret != ARPC_SUCCESS) {
			ARPC_LOG_ERROR("_post_iov_to_async_thread fail.");
			goto free_user_buf;
		}
	}else{
		ARPC_LOG_ERROR("unkown fail.");
		goto free_user_buf;
	}
end:
	return 0;
free_user_buf:
	_clean_header_source(req, ops->free_cb, usr_ctx);

do_respone:	
	/* attach request to response */
	return _do_respone(&rsp.rsp_iov, req, ops->release_rsp_cb, usr_ctx);
}

int _process_send_rsp_complete(struct xio_msg *rsp, struct request_ops *ops, void *usr_ctx)
{
	struct _rsp_complete_ctx *rsp_ctx;
	
	LOG_THEN_RETURN_VAL_IF_TRUE((!rsp || !ops), ARPC_ERROR, "rsp or ops null.");
	ARPC_LOG_DEBUG("rsp_send_complete, rsp:%p.", rsp);
	rsp_ctx = (struct _rsp_complete_ctx *)rsp->user_context;
	rsp->user_context =NULL;

	if(IS_SET(rsp->usr_flags, FLAG_RSP_USER_DATA) && rsp_ctx && rsp_ctx->release_rsp_cb){
		rsp_ctx->release_rsp_cb(&rsp_ctx->rsp_iov, rsp_ctx->rsp_usr_ctx); //释放用户申请的资源
	}

	if (rsp_ctx) {
		ARPC_MEM_FREE(rsp_ctx, NULL); // todo
		rsp_ctx = NULL;

	}

	// 释放内部IOV申请的资源
	if (rsp->out.pdata_iov.sglist) {
		ARPC_MEM_FREE(rsp->out.pdata_iov.sglist, NULL); // todo
		rsp->out.pdata_iov.sglist = NULL;
	}
	ARPC_MEM_FREE(rsp, NULL); // todo

	return 0;
}

