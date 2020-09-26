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
#include <unistd.h>

#include "arpc_process_rsp.h"
#include "arpc_request.h"
#include "arpc_message.h"

// 已加锁
static int arpc_rx_request_rsp_head(struct arpc_common_msg *req_msg, struct arpc_connection *con)
{
	REQUEST_USR_EX_CTX(ex_msg, req_msg);
	LOG_THEN_RETURN_VAL_IF_TRUE((!req_msg), ARPC_ERROR, "req_msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!con), ARPC_ERROR, "con null.");
	return alloc_xio_msg_usr_buf(ex_msg->x_rsp_msg, ex_msg->msg);
}

// REQUEST回复消息处理方式
static int arpc_process_rsp_data(struct arpc_common_msg *req_msg, struct arpc_connection *con)
{
	int ret = ARPC_ERROR;

	REQUEST_CTX(usr_msg, ex_msg, req_msg);

	LOG_THEN_RETURN_VAL_IF_TRUE((!req_msg), ARPC_ERROR, "req_msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!con), ARPC_ERROR, "con null.");

	arpc_cond_lock(&req_msg->cond);	// to lock
	MSG_CLR_REQ(ex_msg->flag);
	MSG_SET_RSP(ex_msg->flag);
	if (!usr_msg->proc_rsp_cb){
		arpc_cond_notify(&req_msg->cond);// 通知
		arpc_cond_unlock(&req_msg->cond);
	}else{
		ret = conver_msg_xio_to_arpc(&ex_msg->x_rsp_msg->in, &usr_msg->receive);
		LOG_ERROR_IF_VAL_TRUE(ret, "conver_msg_xio_to_arpc fail.");
		ret = usr_msg->proc_rsp_cb(&usr_msg->receive, usr_msg->receive_ctx);
		LOG_ERROR_IF_VAL_TRUE(ret, "proc_rsp_cb fail.");
		arpc_cond_unlock(&req_msg->cond);
		arpc_destroy_common_msg(req_msg);	//un lock
	}
	return ret;	
}

int process_rsp_header(struct xio_msg *rsp, struct arpc_connection *con)
{
	struct arpc_common_msg *req_msg = (struct arpc_common_msg *)rsp->user_context;
	REQUEST_USR_EX_CTX(ex_msg, req_msg);

	LOG_THEN_RETURN_VAL_IF_TRUE((req_msg->magic == ARPC_COM_MSG_MAGIC), ARPC_ERROR, "magic[%x] bot match.", req_msg->magic);
	ex_msg->x_rsp_msg = rsp;
	return arpc_rx_request_rsp_head(req_msg, con);
}

int process_rsp_data(struct xio_msg *rsp, int last_in_rxq, struct arpc_connection *con)
{
	struct arpc_common_msg *req_msg = (struct arpc_common_msg *)rsp->user_context;
	REQUEST_USR_EX_CTX(ex_msg, req_msg);

	ex_msg->x_rsp_msg = rsp;
	LOG_THEN_RETURN_VAL_IF_TRUE((req_msg->magic == ARPC_COM_MSG_MAGIC), ARPC_ERROR, "magic[%x] bot match.", req_msg->magic);
	return arpc_process_rsp_data(req_msg, con);
}

