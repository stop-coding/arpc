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

#define SEND_ONEWAY_END_MAX_TIME (2*1000)
#define DO_REQUEST_RSP_MAX_TIME  (5*1000)

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
	uint32_t send_cnt;
	struct arpc_connection *con = NULL;
	struct arpc_msg_ex *ex_msg;
	struct arpc_request_handle *req_fd;

	LOG_THEN_RETURN_VAL_IF_TRUE((!session_ctx || !msg ), ARPC_ERROR, "arpc_session_handle_t fd null, exit.");

	ret = session_get_idle_conn(session_ctx, &con, ARPC_MSG_TYPE_REQ, timeout_ms);
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR,"session_get_idle_conn fail");

	req_msg = get_common_msg(con, ARPC_MSG_TYPE_REQ);
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, ARPC_ERROR,"get_common_msg");

	req_fd = (struct arpc_request_handle*)req_msg->ex_data;

	req_fd->msg = msg;
	req_fd->msg_ex = (struct arpc_msg_ex *)msg->handle;

	ex_msg = req_fd->msg_ex;
	req = &req_msg->xio_msg;
	req_msg->tx_msg = req;

	req->in.sgl_type		= XIO_SGL_TYPE_IOV_PTR;
	ret = convert_msg_arpc2xio(&msg->send, &req->out, &ex_msg->attr);
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
		ret = arpc_connection_async_send(con, req_msg);
		if(ret){
			ret = ARPC_ERROR;
			ARPC_LOG_ERROR("session[%p] send fail or timeout.", session_ctx);
			continue;
		}
		ret = 0;
		break;
	}

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "session[%p] do requet send msg fail.", session_ctx);
	MSG_SET_REQ(ex_msg->flags);
	if (!msg->proc_rsp_cb){
		if (timeout_ms > 0)
			ret = arpc_cond_wait_timeout(&req_msg->cond, timeout_ms + 500);//至少500ms起步
		else
			ret = arpc_cond_wait_timeout(&req_msg->cond, DO_REQUEST_RSP_MAX_TIME); // 默认等待
		if (!ret){
			ex_msg->x_rsp_msg = NULL;
			MSG_CLR_REQ(ex_msg->flags);
			arpc_cond_unlock(&req_msg->cond);
			free_msg_arpc2xio(&req->out);
			put_common_msg(req_msg);
		}else{
			ARPC_LOG_ERROR("wait msg rx respone of request fail.");// todo 内存回收
			ex_msg->x_rsp_msg = NULL;
			MSG_CLR_REQ(ex_msg->flags);
			SET_FLAG(req_msg->flag, XIO_MSG_ERROR_DISCARD_DATA);
			arpc_cond_unlock(&req_msg->cond);
			return (-ETIMEDOUT);
		}
	}else{
		arpc_cond_unlock(&req_msg->cond);
	}
	if (IS_SET(ex_msg->flags, XIO_MSG_ERROR_DISCARD_DATA)){
		return (-ENODATA);
	}
	return 0;
unlock:
	arpc_cond_unlock(&req_msg->cond);
free_common_msg:
	free_msg_arpc2xio(&req->out);
	put_common_msg(req_msg);	//un lock
	return (-ENETUNREACH);	
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
	struct xio_iovec_ex *sglist;
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, ARPC_ERROR, "req_msg null, fail.");

	ret = arpc_cond_lock(&req_msg->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "rsp copmlete cond lock fail, maybe release.");
	req_msg_ex = (struct arpc_request_handle *)req_msg->ex_data;
	SET_FLAG(req_msg_ex->msg_ex->flags, XIO_MSG_RSP);
	MSG_CLR_REQ(req_msg_ex->msg_ex->flags);
	if (req_msg_ex->msg && req_msg_ex->msg->proc_rsp_cb){
		if (IS_SET(req_msg_ex->msg_ex->flags, XIO_MSG_ERROR_DISCARD_DATA)){
			req_msg_ex->msg->proc_rsp_cb(NULL, req_msg_ex->msg->receive_ctx);
		}else{
			req_msg_ex->msg->proc_rsp_cb(&req_msg_ex->msg->receive, req_msg_ex->msg->receive_ctx);
		}
		arpc_cond_unlock(&req_msg->cond);
		free_msg_arpc2xio(&req_msg->xio_msg.out);
		put_common_msg(req_msg);	//un lock
	}else{
		arpc_cond_notify(&req_msg->cond);
		arpc_cond_unlock(&req_msg->cond);
	}
	ARPC_LOG_DEBUG("request get rsp complete.");
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
	uint32_t send_cnt = 0;
	struct arpc_connection *con = NULL;

	LOG_THEN_RETURN_VAL_IF_TRUE((!session_ctx), ARPC_ERROR, "arpc_session_handle_t fd null, exit.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!send ), ARPC_ERROR, " send null, exit.");

	ret = session_get_idle_conn(session_ctx, &con, ARPC_MSG_TYPE_OW, SEND_ONEWAY_END_MAX_TIME);
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR,"session_get_idle_conn fail");


	req_msg = get_common_msg(con, ARPC_MSG_TYPE_OW);
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, ARPC_ERROR,"get_common_msg");

	ARPC_LOG_DEBUG("new msg:%p.", req_msg);//

	ow_msg = (struct arpc_oneway_handle*)req_msg->ex_data;
	ow_msg->send = send;
	ow_msg->clean_send_cb = clean_send;
	ow_msg->send_ctx = send_ctx;
	req = &req_msg->xio_msg;
	req_msg->tx_msg = req;
	req_msg->type = ARPC_MSG_TYPE_OW;

	ret = convert_msg_arpc2xio(send, &req->out, &ow_msg->attr);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_common_msg, "convert xio msg fail.");
	req->user_context = req_msg;
	if (!ow_msg->clean_send_cb){
		req->flags |= XIO_MSG_FLAG_IMM_SEND_COMP;
	}

	arpc_cond_lock(&req_msg->cond);
	while(send_cnt < 1) {
		send_cnt++;
		ret = arpc_connection_async_send(con, req_msg);
		if (ret) {
			ARPC_LOG_ERROR("session[%p] send timeout.", session_ctx);
			ret = ARPC_ERROR;
			continue;
		}
		break;
	}

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "session send msg fail.");
	if (!ow_msg->clean_send_cb){
		ret = arpc_cond_wait_timeout(&req_msg->cond, SEND_ONEWAY_END_MAX_TIME); // 默认等待
		arpc_cond_unlock(&req_msg->cond);
		if (!ret){
			free_msg_arpc2xio(&req->out);
			put_common_msg(req_msg);	//un lock
		}else{
			free_msg_arpc2xio(&req->out);
			ARPC_LOG_ERROR("wait oneway msg send complete timeout fail, msg keep."); // TODO 释放超时的资源
		}
	}else{
		arpc_cond_unlock(&req_msg->cond);
	}
	return ret;
unlock:
	arpc_cond_unlock(&req_msg->cond);
free_common_msg:
	free_msg_arpc2xio(&req->out);
	put_common_msg(req_msg);	//un lock
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
		free_msg_arpc2xio(&ow_msg->xio_msg.out);
		put_common_msg(ow_msg);	//un lock
	}else{
		arpc_cond_notify(&ow_msg->cond);
		arpc_cond_unlock(&ow_msg->cond);
	}
	ARPC_LOG_DEBUG("send end complete.");
	return 0;
}
