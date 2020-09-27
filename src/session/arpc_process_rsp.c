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
	LOG_THEN_RETURN_VAL_IF_TRUE((!req_msg), ARPC_ERROR, "req_msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!con), ARPC_ERROR, "con null.");

	//同步通知数据发送完成
	ret = arpc_session_send_comp_notify(con, req_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_session_send_comp_notify fail.");

	ret = arpc_request_rsp_complete(req_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_request_rsp_complete fail.");

	return ret;	
}

int process_rsp_header(struct xio_msg *rsp, struct arpc_connection *con)
{
	struct arpc_common_msg *req_msg = (struct arpc_common_msg *)rsp->user_context;
	REQUEST_USR_EX_CTX(ex_msg, req_msg);

	LOG_THEN_RETURN_VAL_IF_TRUE((req_msg->magic != ARPC_COM_MSG_MAGIC), ARPC_ERROR, "magic[%x] not match.", req_msg->magic);
	ex_msg->x_rsp_msg = rsp;
	return arpc_rx_request_rsp_head(req_msg, con);
}

int process_rsp_data(struct xio_msg *rsp, int last_in_rxq, struct arpc_connection *con)
{
	struct arpc_common_msg *req_msg = (struct arpc_common_msg *)rsp->user_context;
	REQUEST_USR_EX_CTX(ex_msg, req_msg);

	ex_msg->x_rsp_msg = rsp;
	LOG_THEN_RETURN_VAL_IF_TRUE((req_msg->magic != ARPC_COM_MSG_MAGIC), ARPC_ERROR, "magic[%x] not match.", req_msg->magic);
	return arpc_process_rsp_data(req_msg, con);
}

