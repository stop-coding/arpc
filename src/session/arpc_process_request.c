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

static const int32_t SEND_RSP_END_MAX_TIME_MS = 2*1000;
static int request_msg_async_deal(void *usr_ctx);

int process_request_header(struct arpc_connection *con, struct xio_msg *msg)
{
	struct proc_header_func head_ops;
	struct request_ops *ops;
	int ret;
	struct arpc_msg_attr msg_attr = {0};
	struct timeval 		tx_time;

	ops = &(arpc_get_ops(con)->req_ops);
	head_ops.alloc_cb = ops->alloc_cb;
	head_ops.free_cb = ops->free_cb;
	head_ops.proc_head_cb = ops->proc_head_cb;

	ret = create_xio_msg_usr_buf(msg, &head_ops, arpc_get_max_iov_len(con), arpc_get_ops_ctx(con), &msg_attr);

	tx_time.tv_sec = msg_attr.tx_sec;
	tx_time.tv_usec = msg_attr.tx_usec;
	if(arpc_get_conn_type(con) == ARPC_CON_TYPE_SERVER && msg_attr.conn_id >= ARPC_CONN_ID_OFFSET && con->id != msg_attr.conn_id){
		ARPC_LOG_NOTICE("server session modify conn id from[%u] to [%u]", con->id, msg_attr.conn_id);
		con->id = msg_attr.conn_id;
	}
	statistics_per_time(&tx_time, &con->rx_req_send, 5);

	return ret;
}

int process_request_data(struct arpc_connection *con, struct xio_msg *req, int last_in_rxq)
{
	uint32_t			nents = vmsg_sglist_nents(&req->in);
	uint32_t			i;
	struct arpc_vmsg 	rev_iov;
	struct arpc_rsp 	usr_rsp_param;
	struct arpc_common_msg *rsp_msg;
	struct arpc_rsp_handle	*rsp_fd_ex;
	struct arpc_thread_param *async_param;
	int						ret;
	struct arpc_msg_attr	attr = {0};
	struct request_ops *ops;
	void *usr_ctx;
	struct timeval 		tx_time;

	LOG_THEN_RETURN_VAL_IF_TRUE((!req), ARPC_ERROR, "req null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!con), ARPC_ERROR, "con null.");

	ops = &(arpc_get_ops(con)->req_ops);
	usr_ctx = arpc_get_ops_ctx(con);

	memset(&rev_iov, 0, sizeof(struct arpc_vmsg));
	
	ret = move_msg_xio2arpc(&req->in, &rev_iov, &attr);
	LOG_THEN_RETURN_VAL_IF_TRUE((ret), ARPC_ERROR, "move_msg_xio2arpc fail.");

	ret = destroy_xio_msg_usr_buf(req, ops->free_cb, usr_ctx);
	LOG_THEN_RETURN_VAL_IF_TRUE((ret), ARPC_ERROR, "destroy_xio_msg_usr_buf fail.");

	rsp_msg = get_common_msg(con, ARPC_MSG_TYPE_RSP);
	LOG_THEN_RETURN_VAL_IF_TRUE(!rsp_msg, ARPC_ERROR, "rsp_msg alloc null.");
	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_msg->ex_data;
	rsp_fd_ex->x_rsp_msg = req;//保存回复的结构体
	rsp_msg->attr.rsp_crc = attr.req_crc;//请求保存在回复体里
	rsp_msg->attr.req_crc = 0;
	memset(&usr_rsp_param, 0, sizeof(struct arpc_rsp));
	usr_rsp_param.rsp_fd = (void *)rsp_msg;

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
		async_param->threadpool = arpc_get_conn_threadpool(con);
		async_param->ops.alloc_cb = ops->alloc_cb;
		async_param->ops.free_cb = ops->free_cb;
		async_param->ops.proc_async_cb = ops->proc_async_cb;
		async_param->ops.release_rsp_cb = ops->release_rsp_cb;
		async_param->ops.proc_oneway_async_cb = NULL;
		async_param->rsp_ctx = rsp_msg;
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
	/* 框架内回复，则不需要通知模式，也不需要加锁 */
	ARPC_LOG_TRACE("do respone request msg.");
	gettimeofday(&tx_time, NULL);	// 线程安全

	rsp_msg->attr.tx_sec= tx_time.tv_sec;
	rsp_msg->attr.tx_usec= tx_time.tv_usec;
	rsp_msg->attr.conn_id = con->id;

	ret = arpc_init_response(rsp_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_init_response fail.");
	ret = xio_send_response(rsp_msg->tx_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "xio_send_response fail.");

	return ret;
}

static int request_msg_async_deal(void *usr_ctx)
{
	struct arpc_thread_param *async = (struct arpc_thread_param *)usr_ctx;
	struct arpc_rsp rsp;
	int ret;
	struct arpc_common_msg *rsp_msg;
	struct arpc_rsp_handle	*rsp_fd_ex;
	struct timeval 		tx_time;

	LOG_THEN_RETURN_VAL_IF_TRUE(!async, ARPC_ERROR, "async null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!async->ops.proc_async_cb, ARPC_ERROR, "request proc_async_cb null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!async->rsp_ctx, ARPC_ERROR, "request rsp context is null.");

	rsp_msg  = (struct arpc_common_msg *)async->rsp_ctx;
	memset(&rsp, 0, sizeof (struct arpc_rsp));						
	rsp.rsp_fd = (void*)rsp_msg;

	ARPC_LOG_TRACE("process request msg with async.");
	ret = async->ops.proc_async_cb(&async->rev_iov, &rsp, async->usr_ctx);
	ARPC_LOG_TRACE("process request msg with end async.");

	LOG_ERROR_IF_VAL_TRUE(ret, "proc_async_cb of request error.");

	if (!IS_SET(rsp.flags, METHOD_CALLER_HIJACK_RX_DATA)) {
		free_msg_xio2arpc(&async->rev_iov, async->ops.free_cb, async->usr_ctx);
		async->rev_iov.vec = NULL;
	}

	rsp_fd_ex = (struct arpc_rsp_handle*)rsp_msg->ex_data;
	rsp_fd_ex->rsp_usr_iov = rsp.rsp_iov;
	rsp_fd_ex->rsp_usr_ctx = rsp.rsp_ctx;
	rsp_fd_ex->release_rsp_cb = async->ops.release_rsp_cb;

	if (!IS_SET(rsp.flags, METHOD_CALLER_ASYNC)) {
		gettimeofday(&tx_time, NULL);	// 线程安全
		arpc_cond_lock(&rsp_msg->cond);
		rsp_msg->attr.tx_sec= tx_time.tv_sec;
		rsp_msg->attr.tx_usec= tx_time.tv_usec;
		ret = arpc_init_response(rsp_msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_init_response fail.");
		ret = arpc_connection_async_send(rsp_msg->conn, rsp_msg);
		if(!ret) {
			ret = arpc_cond_wait_timeout(&rsp_msg->cond, SEND_RSP_END_MAX_TIME_MS); // 默认等待
			LOG_ERROR_IF_VAL_TRUE(ret, "rsp send timeout fail, conn[%u], msg[%p].", rsp_msg->conn->id, rsp_msg);
		}else{
			ARPC_LOG_ERROR("arpc_connection_async_send fail, conn[%u], msg[%p].", rsp_msg->conn->id, rsp_msg);
		}
		arpc_cond_unlock(&rsp_msg->cond);
	}
	SAFE_FREE_MEM(async->rev_iov.head);
	SAFE_FREE_MEM(async);

	return 0;
}