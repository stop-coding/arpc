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
#include "arpc_process_oneway.h"

static int oneway_msg_async_deal(void *usr_ctx);

int process_oneway_header(struct arpc_connection *con, struct xio_msg *msg)
{
	struct proc_header_func head_ops;
	struct arpc_msg_attr msg_attr = {0};
	int ret;
	struct timeval 		tx_time;
	struct oneway_ops *ops;

	ops = &(arpc_get_ops(con)->oneway_ops);

	head_ops.alloc_cb = ops->alloc_cb;
	head_ops.free_cb = ops->free_cb;
	head_ops.proc_head_cb = ops->proc_head_cb;
	ARPC_LOG_TRACE("get oneway msg head");
	ret = create_xio_msg_usr_buf(msg, &head_ops, arpc_get_max_iov_len(con), arpc_get_ops_ctx(con), &msg_attr);
	tx_time.tv_sec = msg_attr.tx_sec;
	tx_time.tv_usec = msg_attr.tx_usec;
	statistics_per_time(&tx_time, &con->rx_ow_send, 5);
	if(arpc_get_conn_type(con) == ARPC_CON_TYPE_SERVER && msg_attr.conn_id >= ARPC_CONN_ID_OFFSET && con->id != msg_attr.conn_id){
		ARPC_LOG_NOTICE("server session modify conn id from[%u] to [%u]", con->id, msg_attr.conn_id);
		con->id = msg_attr.conn_id;
	}
	return ret;
}

int process_oneway_data(struct arpc_connection *con, struct xio_msg *req, int last_in_rxq)
{
	uint32_t					nents = vmsg_sglist_nents(&req->in);
	uint32_t					i;
	int							ret;
	struct arpc_vmsg 			rev_iov;
	uint32_t 				 	flags = 0;
	struct arpc_thread_param 	*async_param;
	struct oneway_ops 			*ops;
	void 						*ops_ctx;

	LOG_THEN_RETURN_VAL_IF_TRUE((!req), ARPC_ERROR, "req null.");
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(IS_SET(req->usr_flags, XIO_MSG_ERROR_DISCARD_DATA), free_data, "dicard data.");

	memset(&rev_iov, 0, sizeof(struct arpc_vmsg));

	ops = &(arpc_get_ops(con)->oneway_ops);
	ops_ctx = arpc_get_ops_ctx(con);

	ARPC_LOG_TRACE("get oneway msg data");
	ret = move_msg_xio2arpc(&req->in, &rev_iov, NULL);
	LOG_THEN_RETURN_VAL_IF_TRUE((ret), ARPC_ERROR, "move_msg_xio2arpc fail.");

	ret = destroy_xio_msg_usr_buf(req, ops->free_cb, ops_ctx);
	LOG_THEN_RETURN_VAL_IF_TRUE((ret), ARPC_ERROR, "destroy_xio_msg_usr_buf fail.");
	if (IS_SET(req->usr_flags, METHOD_ARPC_PROC_SYNC) && ops->proc_data_cb) {
		ARPC_LOG_TRACE("process rx oneway msg with sync.");
		ret = ops->proc_data_cb(&rev_iov, &flags, ops_ctx);
		ARPC_LOG_TRACE("process rx oneway msg finished with sync, flag[0x%x].", flags);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_data, "proc_data_cb  return fail.");

		if(!IS_SET(flags, METHOD_CALLER_HIJACK_RX_DATA)){
			free_msg_xio2arpc(&rev_iov, ops->free_cb, ops_ctx);
		}
	}else {
		ARPC_LOG_DEBUG("set proc_async_cb data.");
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!IS_SET(req->usr_flags, METHOD_ALLOC_DATA_BUF) && nents, 
										free_data, "system alloc memory do not allowe deal on async, nents[%u].", nents);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ops->free_cb, free_data, "free_cb is null, can't do async.");
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ops->proc_async_cb, free_data, "proc_async_cb is null, can't do async.");

		async_param = (struct arpc_thread_param * )arpc_mem_alloc(sizeof(struct arpc_thread_param), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!async_param, free_data, "async_param is null, can't do async.");

		memset(async_param, 0, sizeof(struct arpc_thread_param));
		async_param->threadpool = arpc_get_conn_threadpool(con);
		async_param->ops.alloc_cb = ops->alloc_cb;
		async_param->ops.free_cb = ops->free_cb;
		async_param->ops.proc_async_cb = NULL;
		async_param->ops.release_rsp_cb = NULL;
		async_param->ops.proc_oneway_async_cb = ops->proc_async_cb;
		async_param->rsp_ctx = NULL;
		async_param->req_msg = NULL;
		async_param->rev_iov = rev_iov;
		async_param->usr_ctx = ops_ctx;
		async_param->loop = oneway_msg_async_deal;

		ret = post_to_async_thread(async_param);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_data, "post_to_async_thread fail.");
	}

	xio_release_msg(req);// 同步释放资源
	return 0;

free_data:
	free_msg_xio2arpc(&rev_iov, ops->free_cb, ops_ctx);
	xio_release_msg(req);// 同步释放资源
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

	ARPC_LOG_TRACE("process rx oneway msg with async.");
	ret = async->ops.proc_oneway_async_cb(&async->rev_iov, &flags, async->usr_ctx);
	ARPC_LOG_TRACE("process rx oneway msg finished with async, flag[0x%x].", flags);

	LOG_ERROR_IF_VAL_TRUE(ret, "proc_oneway_async_cb error.");
	if (!IS_SET(flags, METHOD_CALLER_HIJACK_RX_DATA)){
		ARPC_LOG_DEBUG("free_msg_xio2arpc data.");
		free_msg_xio2arpc(&async->rev_iov, async->ops.free_cb, async->usr_ctx);
	}
	// free
	SAFE_FREE_MEM(async->rev_iov.head);
	SAFE_FREE_MEM(async);
	return 0;
}
