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

#include "queue.h"
#include "arpc_com.h"
#include "arpc_request.h"
#include "arpc_message.h"

#define SEND_ONEWAY_END_MAX_TIME (10*1000)
#define DO_REQUEST_RSP_MAX_TIME  (30*1000)

/**
 * 发送一个请求消息
 * @param[in] fd ,a session handle
 * @param[in] msg ,a data that will send
 * @return receive .0,表示发送成功，小于0则失败
 */
int arpc_do_request(const arpc_session_handle_t fd, struct arpc_msg *msg, int32_t timeout_ms)
{
	struct arpc_session_handle *session_ctx = (struct arpc_session_handle *)fd;
	struct xio_msg 	*req = NULL;
	int ret = ARPC_ERROR;
	struct arpc_common_msg *req_msg = NULL;
	struct arpc_msg *rev_msg;
	struct arpc_connection *con;
	uint32_t send_cnt;
	struct arpc_msg_ex *ex_msg;
	struct arpc_request_handle *req_fd;

	LOG_THEN_RETURN_VAL_IF_TRUE((!session_ctx || !msg ), ARPC_ERROR, "arpc_session_handle_t fd null, exit.");

	req_msg = arpc_create_common_msg(sizeof(struct arpc_request_handle));
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, ARPC_ERROR,"arpc_create_common_msg");
	req_fd = (struct arpc_request_handle*)req_msg->ex_data;

	req_fd->msg = msg;
	req_fd->msg_ex = (struct arpc_msg_ex *)msg->handle;

	ex_msg = req_fd->msg_ex;
	req = &ex_msg->x_req_msg;
	req_msg->tx_msg = req;

	// get msg
	ret = conver_msg_arpc_to_xio(&msg->send, &req->out);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_common_msg, "convert xio msg fail.");
	req->user_context = req_msg;
	ex_msg = (struct arpc_msg_ex *)msg->handle;

	send_cnt = 0;
	if (!msg->proc_rsp_cb){
		req->flags |= XIO_MSG_FLAG_IMM_SEND_COMP;
	}
	arpc_cond_lock(&req_msg->cond);
	while(send_cnt < 1) {
		send_cnt++;
		ret = session_get_conn(session_ctx, &con, timeout_ms);
		if (ret || !con) {
			ARPC_LOG_ERROR("session[%p] wait idle connection timeout.", session_ctx);
			ret = ARPC_ERROR;
			break;
		}
		ret = arpc_session_async_send(con, req_msg);
		if(ret){
			ARPC_LOG_ERROR("xio_send_request fail, conn[%u][%p], xio con[%p].",con->id, con, con->xio_con);
			ret = ARPC_ERROR;
			continue;
		}
		ret = 0;
		break;
	}

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "session[%p] do requet send msg fail.", session_ctx);
	MSG_SET_REQ(ex_msg->flags);
	if (!msg->proc_rsp_cb){
		if (timeout_ms > 0)
			ret = arpc_cond_wait_timeout(&req_msg->cond, timeout_ms + DO_REQUEST_RSP_MAX_TIME);
		else
			ret = arpc_cond_wait_timeout(&req_msg->cond, DO_REQUEST_RSP_MAX_TIME); // 默认等待
		if (!ret){
			ex_msg->x_rsp_msg = NULL;
			MSG_CLR_REQ(ex_msg->flags);
			arpc_cond_unlock(&req_msg->cond);
			arpc_destroy_common_msg(req_msg);
		}else{
			ARPC_LOG_ERROR("wait con[%u][%p] rx respone of request fail.", con->id, con);
			ex_msg->x_rsp_msg = NULL;
			MSG_CLR_REQ(ex_msg->flags);
			arpc_cond_unlock(&req_msg->cond);
			//arpc_destroy_common_msg(req_msg);	//un lock
			return -1;
		}
	}else{
		arpc_cond_unlock(&req_msg->cond);
	}
	return 0;
unlock:
	arpc_cond_unlock(&req_msg->cond);
free_common_msg:
	arpc_destroy_common_msg(req_msg);	//un lock
	return ARPC_ERROR;	
}

/**
 * 释放request消息,必须在回调内执行
 * @param[in] oneway_msg ,a session handle
 * @return receive .0,表示发送成功，小于0则失败
 */
int arpc_request_rsp_complete(struct arpc_common_msg *req_msg)
{
	int ret;
	struct arpc_request_handle *req_msg_ex;
	struct xio_iovec_ex *out_addr;
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, ARPC_ERROR, "req_msg null, fail.");

	ret = arpc_cond_lock(&req_msg->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "rsp copmlete cond lock fail, maybe release.");
	req_msg_ex = (struct arpc_request_handle *)req_msg->ex_data;
	
	if(req_msg_ex->msg_ex->x_rsp_msg){
		ret = conver_msg_xio_to_arpc(&req_msg_ex->msg_ex->x_rsp_msg->in, &req_msg_ex->msg->receive);
		LOG_ERROR_IF_VAL_TRUE(ret, "conver_msg_xio_to_arpc fail");
		ret = xio_release_response(req_msg_ex->msg_ex->x_rsp_msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "xio_release_response fail");
		req_msg_ex->msg_ex->x_rsp_msg = NULL;
	}
	release_arpc2xio_msg(&req_msg_ex->msg_ex->x_req_msg.out);
	if (req_msg_ex->msg && req_msg_ex->msg->proc_rsp_cb){
		req_msg_ex->msg->proc_rsp_cb(&req_msg_ex->msg->receive, req_msg_ex->msg->receive_ctx);
		arpc_cond_unlock(&req_msg->cond);
		arpc_destroy_common_msg(req_msg);	//un lock
	}else{
		arpc_cond_notify(&req_msg->cond);
		arpc_cond_unlock(&req_msg->cond);
	}
	ARPC_LOG_DEBUG("send end complete.");
	return 0;
}


/**
 * 发送一个单向消息（接收方无需回复）
 * @param[in] fd ,a session handle
 * @param[in] msg ,a data that will send
 * @return receive .0,表示发送成功，小于0则失败
 */
int arpc_send_oneway_msg(const arpc_session_handle_t fd, struct arpc_vmsg *send, clean_send_cb_t clean_send, void *send_ctx)
{
	struct arpc_session_handle *session_ctx = (struct arpc_session_handle *)fd;
	struct xio_msg 	*req = NULL;
	int ret = ARPC_ERROR;
	struct arpc_common_msg *req_msg = NULL;
	struct arpc_oneway_handle *ow_msg;
	struct arpc_connection *con;
	uint32_t send_cnt = 0;

	LOG_THEN_RETURN_VAL_IF_TRUE((!session_ctx), ARPC_ERROR, "arpc_session_handle_t fd null, exit.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!send ), ARPC_ERROR, " send null, exit.");

	req_msg = arpc_create_common_msg(sizeof(struct arpc_oneway_handle));
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, ARPC_ERROR,"arpc_create_common_msg");
	ow_msg = (struct arpc_oneway_handle*)req_msg->ex_data;
	ow_msg->send = send;
	ow_msg->clean_send_cb = clean_send;
	ow_msg->send_ctx = send_ctx;
	req = &ow_msg->x_req_msg;
	req_msg->tx_msg = req;
	req_msg->type = ARPC_MSG_TYPE_OW;
	// get msg
	ret = conver_msg_arpc_to_xio(send, &req->out);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_common_msg, "convert xio msg fail.");
	req->user_context = req_msg;
	if (!ow_msg->clean_send_cb){
		req->flags |= XIO_MSG_FLAG_IMM_SEND_COMP;
	}
	arpc_cond_lock(&req_msg->cond);
	while(send_cnt < 1) {
		send_cnt++;
		ret = session_get_conn(session_ctx, &con, SEND_ONEWAY_END_MAX_TIME);
		if (ret || !con) {
			ARPC_LOG_ERROR("session[%p] wait idle connection timeout.", session_ctx);
			ret = ARPC_ERROR;
			break;
		}
		ret = arpc_session_async_send(con, req_msg);
		if(ret){
			ARPC_LOG_ERROR("xio_send_request fail, conn[%u][%p], xio con[%p].",con->id, con, con->xio_con);
			ret = ARPC_ERROR;
			continue;
		}
		ret = 0;
		break;
	}
	
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "session send msg fail.");

	if (!ow_msg->clean_send_cb){
		ret = arpc_cond_wait_timeout(&req_msg->cond, SEND_ONEWAY_END_MAX_TIME); // 默认等待
		LOG_ERROR_IF_VAL_TRUE(ret, "con[%u][%p] wait oneway msg send complete timeout fail.", con->id, con);
		arpc_cond_unlock(&req_msg->cond);
		arpc_destroy_common_msg(req_msg);	//un lock
	}else{
		arpc_cond_unlock(&req_msg->cond);
	}
	return ret;
unlock:
	arpc_cond_unlock(&req_msg->cond);
free_common_msg:
	arpc_destroy_common_msg(req_msg);	//un lock
	return ARPC_ERROR;
}

/**
 * 释放发送一个单向消息
 * @param[in] oneway_msg ,a session handle
 * @return receive .0,表示发送成功，小于0则失败
 */
int arpc_oneway_send_complete(struct arpc_common_msg *ow_msg)
{
	int ret;
	struct arpc_oneway_handle *ow_msg_ex;
	LOG_THEN_RETURN_VAL_IF_TRUE(!ow_msg, ARPC_ERROR, "ow_msg null, fail.");

	ret = arpc_cond_lock(&ow_msg->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "send copmlete cond lock fail, maybe release.");
	ow_msg_ex = (struct arpc_oneway_handle *)ow_msg->ex_data;
	if (ow_msg_ex->clean_send_cb){
		ow_msg_ex->clean_send_cb(ow_msg_ex->send, ow_msg_ex->send_ctx);
		arpc_cond_unlock(&ow_msg->cond);
		arpc_destroy_common_msg(ow_msg);	//un lock
	}else{
		arpc_cond_notify(&ow_msg->cond);
		arpc_cond_unlock(&ow_msg->cond);
	}

	ARPC_LOG_DEBUG("send end complete.");
	return 0;
}
