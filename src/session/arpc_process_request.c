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

#include "arpc_com.h"
#include "threadpool.h"
#include "arpc_request.h"
#include "arpc_response.h"

static int request_msg_async_deal(void *usr_ctx);

int process_request_header(struct arpc_connection *con, struct xio_msg *msg, struct request_ops *ops, uint64_t iov_max_len, void *usr_ctx)
{
	struct proc_header_func head_ops;
	head_ops.alloc_cb = ops->alloc_cb;
	head_ops.free_cb = ops->free_cb;
	head_ops.proc_head_cb = ops->proc_head_cb;
	return create_xio_msg_usr_buf(msg, &head_ops, iov_max_len, usr_ctx);
}

int process_request_data(struct arpc_connection *con, struct xio_msg *req, struct request_ops *ops, int last_in_rxq, void *usr_ctx)
{
	uint32_t			nents = vmsg_sglist_nents(&req->in);
	uint32_t			i;
	struct arpc_vmsg 	rev_iov;
	struct arpc_rsp 	usr_rsp_param;
	struct arpc_common_msg *rsp_hanlde;
	struct arpc_rsp_handle	*rsp_fd_ex;
	struct arpc_thread_param *async_param;
	int					ret;
	struct arpc_msg_attr	attr = {0};

	LOG_THEN_RETURN_VAL_IF_TRUE((!req), ARPC_ERROR, "req null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!ops), ARPC_ERROR, "ops null.");

	memset(&rev_iov, 0, sizeof(struct arpc_vmsg));
	
	ret = move_msg_xio2arpc(&req->in, &rev_iov, &attr);
	LOG_THEN_RETURN_VAL_IF_TRUE((ret), ARPC_ERROR, "move_msg_xio2arpc fail.");

	ret = destroy_xio_msg_usr_buf(req, ops->free_cb, usr_ctx);
	LOG_THEN_RETURN_VAL_IF_TRUE((ret), ARPC_ERROR, "destroy_xio_msg_usr_buf fail.");

	rsp_hanlde = get_common_msg(con, ARPC_MSG_TYPE_RSP);
	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_hanlde, ARPC_ERROR, "rsp_hanlde alloc null.");
	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_hanlde->ex_data;
	rsp_fd_ex->x_rsp_msg = req;//保存回复的结构体
	rsp_fd_ex->attr.rsp_crc = attr.req_crc;//请求保存在回复体里
	rsp_fd_ex->attr.req_crc = 0;
	memset(&usr_rsp_param, 0, sizeof(struct arpc_rsp));
	usr_rsp_param.rsp_fd = (void *)rsp_hanlde;

	if(IS_SET(req->usr_flags, METHOD_ARPC_PROC_SYNC) && ops->proc_data_cb){
		ARPC_LOG_TRACE("process rx request msg with async.");
		ret = ops->proc_data_cb(&rev_iov, &usr_rsp_param, usr_ctx);
		ARPC_LOG_TRACE("process rx request msg end with async.");

		LOG_ERROR_IF_VAL_TRUE(ret, "proc_data_cb that define for user is error.");
		if (!IS_SET(usr_rsp_param.flags, METHOD_CALLER_HIJACK_RX_DATA)) {
			free_msg_xio2arpc(&rev_iov, ops->free_cb, usr_ctx);
		}

		if (!IS_SET(usr_rsp_param.flags, METHOD_CALLER_ASYNC)) {
			rsp_fd_ex->rsp_usr_iov = usr_rsp_param.rsp_iov;
			rsp_fd_ex->rsp_usr_ctx = usr_rsp_param.rsp_ctx;
			rsp_fd_ex->release_rsp_cb = ops->release_rsp_cb;
			goto do_respone;
		}
	}else if(ops->free_cb && ops->proc_async_cb && ops->release_rsp_cb){
		if (!IS_SET(req->usr_flags, METHOD_ALLOC_DATA_BUF) && nents) {
			ARPC_LOG_ERROR("caller don't alloc buf to rx data, can't proc async fail.");
			goto free_user_buf;
		}
		async_param = (struct arpc_thread_param * )arpc_mem_alloc(sizeof(struct arpc_thread_param), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!async_param, free_user_buf, "async_param is null, can't do async.");
		memset(async_param, 0, sizeof(struct arpc_thread_param));
		async_param->ops.alloc_cb = ops->alloc_cb;
		async_param->ops.free_cb = ops->free_cb;
		async_param->ops.proc_async_cb = ops->proc_async_cb;
		async_param->ops.release_rsp_cb = ops->release_rsp_cb;
		async_param->ops.proc_oneway_async_cb = NULL;
		async_param->rsp_ctx = rsp_hanlde;
		async_param->rev_iov = rev_iov;
		async_param->req_msg = NULL;
		async_param->usr_ctx = usr_ctx;
		async_param->loop = &request_msg_async_deal;

		ret = post_to_async_thread(async_param);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_user_buf, "post_to_async_thread fail, can't do async.");
	}else{
		ARPC_LOG_ERROR("unkown fail.");
		goto free_user_buf;
	}

	return 0;
free_user_buf:
	free_msg_xio2arpc(&rev_iov, ops->free_cb, usr_ctx);

do_respone:	
	/* attach request to response */
	ARPC_LOG_TRACE("do respone request msg.");
	ret = arpc_init_response(rsp_hanlde);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_init_response fail.");
	if(!ret){
		ret = arpc_connection_async_send(rsp_hanlde->conn, rsp_hanlde);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_connection_async_send fail.");
	}
	return ret;
}

static int request_msg_async_deal(void *usr_ctx)
{
	struct arpc_thread_param *async = (struct arpc_thread_param *)usr_ctx;
	struct arpc_rsp rsp;
	int ret;
	struct arpc_common_msg *rsp_fd;
	struct arpc_rsp_handle	*rsp_fd_ex;

	LOG_THEN_RETURN_VAL_IF_TRUE(!async, ARPC_ERROR, "async null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!async->ops.proc_async_cb, ARPC_ERROR, "request proc_async_cb null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!async->rsp_ctx, ARPC_ERROR, "request rsp context is null.");

	rsp_fd  = (struct arpc_common_msg *)async->rsp_ctx;
	memset(&rsp, 0, sizeof (struct arpc_rsp));						
	rsp.rsp_fd = (void*)rsp_fd;

	ARPC_LOG_TRACE("process request msg with async.");
	ret = async->ops.proc_async_cb(&async->rev_iov, &rsp, async->usr_ctx);
	ARPC_LOG_TRACE("process request msg with end async.");

	LOG_ERROR_IF_VAL_TRUE(ret, "proc_async_cb of request error.");

	if (!IS_SET(rsp.flags, METHOD_CALLER_HIJACK_RX_DATA)) {
		free_msg_xio2arpc(&async->rev_iov, async->ops.free_cb, async->usr_ctx);
		async->rev_iov.vec = NULL;
	}

	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_fd->ex_data;
	rsp_fd_ex->rsp_usr_iov = rsp.rsp_iov;
	rsp_fd_ex->release_rsp_cb = async->ops.release_rsp_cb;

	if (!IS_SET(rsp.flags, METHOD_CALLER_ASYNC)) {
		ret = arpc_init_response(rsp_fd);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_do_respone fail.");
		if(!ret){
			ret = arpc_connection_async_send(rsp_fd->conn, rsp_fd);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_do_respone fail.");
		}
	}
	SAFE_FREE_MEM(async->rev_iov.head);
	SAFE_FREE_MEM(async);

	return 0;
}