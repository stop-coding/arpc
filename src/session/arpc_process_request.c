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


static const char SERVER_DEFAULT[] = "rsp-header:undefine";

int _process_request_header(struct xio_msg *msg, struct request_ops *ops, uint64_t iov_max_len, void *usr_ctx)
{
	struct _proc_header_func head_ops;
	head_ops.alloc_cb = ops->alloc_cb;
	head_ops.free_cb = ops->free_cb;
	head_ops.proc_head_cb = ops->proc_head_cb;
	return _create_header_source(msg, &head_ops, iov_max_len, usr_ctx);
}

static int _do_respone(struct arpc_vmsg *rsp_iov, struct xio_msg  *req)
{
	struct xio_msg  	*rsp_msg;
	uint32_t			i;

	rsp_msg = (struct xio_msg*)ARPC_MEM_ALLOC(sizeof(struct xio_msg), NULL); // todo queue
	memset(rsp_msg, 0, sizeof(struct xio_msg));
	if(rsp_iov && rsp_iov->head){
		ARPC_LOG_DEBUG("rsp  header.");
		rsp_msg->out.header.iov_base = rsp_iov->head;
		rsp_msg->out.header.iov_len  = rsp_iov->head_len;
	}else{
		rsp_msg->out.header.iov_base = (char*)SERVER_DEFAULT;
		rsp_msg->out.header.iov_len  = sizeof(SERVER_DEFAULT);
		rsp_msg->out.sgl_type = XIO_SGL_TYPE_IOV_PTR;
		vmsg_sglist_set_nents(&rsp_msg->out, 0);
	}

	if (rsp_iov && rsp_iov->vec && rsp_iov->total_data && rsp_iov->vec_num) {
		ARPC_LOG_DEBUG("rsp  data.");
		rsp_msg->out.total_data_len  = rsp_iov->total_data;
		rsp_msg->out.sgl_type = XIO_SGL_TYPE_IOV_PTR;
		rsp_msg->out.pdata_iov.sglist = ARPC_MEM_ALLOC(rsp_iov->vec_num * sizeof(struct xio_iovec_ex), NULL);
		for (i = 0; i < rsp_iov->vec_num; i++){
			rsp_msg->out.pdata_iov.sglist[i].iov_base = rsp_iov->vec[i].data;
			rsp_msg->out.pdata_iov.sglist[i].iov_len = rsp_iov->vec[i].len;
			rsp_msg->out.pdata_iov.sglist[i].mr = NULL;
			rsp_msg->out.pdata_iov.sglist[i].user_context = NULL;
		}
		rsp_msg->out.pdata_iov.max_nents = rsp_iov->vec_num;
		vmsg_sglist_set_nents(&rsp_msg->out, rsp_iov->vec_num);
		rsp_msg->user_context = (void*)rsp_iov->vec;
		SET_FLAG(rsp_msg->usr_flags, FLAG_RSP_USER_DATA);
	}

	rsp_msg->request = req;
	xio_send_response(rsp_msg);
	return 0;
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
	struct arpc_vmsg 	rsp_iov;
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
	memset(&rsp_iov, 0, sizeof(struct arpc_vmsg));
	if(ops->proc_data_cb){
		ret = ops->proc_data_cb(&rev_iov, &rsp_iov, usr_ctx);
		LOG_ERROR_IF_VAL_TRUE(ret, "proc_data_cb that define for user is error.");
	}

	// 异步处理
	if(IS_SET(req->usr_flags, METHOD_PROCESS_ASYNC) && 
		IS_SET(req->usr_flags, METHOD_ALLOC_DATA_BUF) && 
		ops->free_cb && ops->proc_async_cb){
		async_ops.alloc_cb = ops->alloc_cb;
		async_ops.free_cb = ops->free_cb;
		async_ops.proc_async_cb = ops->proc_async_cb;
		ret = _post_iov_to_async_thread(&rev_iov, NULL, &async_ops, usr_ctx);
		if(ret != ARPC_SUCCESS) {
			ARPC_LOG_ERROR("_post_iov_to_async_thread fail.");
			goto free_user_buf;
		}else{
			goto do_respone;
		}
	}

free_user_buf:
	_clean_header_source(req, ops->free_cb, usr_ctx);

do_respone:	
	/* attach request to response */
	return _do_respone(&rsp_iov, req);
}

int _process_send_rsp_complete(struct xio_msg *rsp, struct request_ops *ops, void *usr_ctx)
{
	struct arpc_vmsg 	rsp_iov;
	LOG_THEN_RETURN_VAL_IF_TRUE((!rsp || !ops), ARPC_ERROR, "rsp or ops null.");
	ARPC_LOG_DEBUG("rsp_send_complete, rsp:%p.", rsp);
	if(IS_SET(rsp->usr_flags, FLAG_RSP_USER_DATA) && ops->release_rsp_cb){
		rsp_iov.head = rsp->out.header.iov_base;
		rsp_iov.head_len = rsp->out.header.iov_len;
		rsp_iov.total_data = rsp->out.total_data_len;
		rsp_iov.vec = (struct arpc_iov*)rsp->user_context;
		rsp_iov.vec_num = rsp->out.data_tbl.nents;
		ops->release_rsp_cb(&rsp_iov, usr_ctx); //释放用户申请的资源
	}
	// 释放内部IOV申请的资源
	if (rsp->out.pdata_iov.sglist) {
		ARPC_MEM_FREE(rsp->out.pdata_iov.sglist, NULL); // todo
		rsp->out.pdata_iov.sglist = NULL;
	}
	ARPC_MEM_FREE(rsp, NULL); // todo

	return 0;
}

