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

#include "arpc_com.h"
#include "arpc_session.h"
#include "arpc_request.h"
#include "arpc_response.h"
#include "arpc_process_rsp.h"
#include "arpc_process_request.h"
#include "arpc_process_oneway.h"

struct arpc_new_session_ctx{
	void *new_session_usr_ctx;
	struct arpc_server_handle *server;
	void *server_usr_ctx;
};

/*! 
 * @brief 
 * 
 * 
 * 
 * @param[in] 
 * @param[in] 
 * @param[in] 
 * @param[in] 
 * @return int .0,表示发送成功，小于0则失败
 */
int server_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *session_context)
{
	struct xio_connection_attr attr;
	int ret;
	struct arpc_connection *con;
	struct arpc_work_handle *work;
	struct arpc_new_session_ctx *session_ctx;
	SESSION_CTX(session_fd, session_context);

	ARPC_LOG_NOTICE("##### session event:%d ,%s. reason: %s.",event_data->event,
					xio_session_event_str(event_data->event),
	       			xio_strerror(event_data->reason));
	session_ctx = (struct arpc_new_session_ctx *)session_fd->ex_ctx;
	switch (event_data->event) {
		case XIO_SESSION_NEW_CONNECTION_EVENT:	//新的链路
			if(event_data->conn_user_context == session_ctx->server){
				ARPC_LOG_NOTICE("##### new connection[%p] is main thread,not need build.", event_data->conn);
				break;
			}
			work = (struct arpc_work_handle *)event_data->conn_user_context;
			con = arpc_create_connection(ARPC_CON_TYPE_SERVER, session_fd, 0);
			LOG_THEN_RETURN_VAL_IF_TRUE(!con, -1, "arpc_create_connection fail.");
			con->xio_con = event_data->conn;
			con->server.work = work;
			con->xio_con_ctx = work->work_ctx;
			con->status = XIO_STA_RUN_ACTION;

			ret = arpc_add_tx_event_to_conn(con);
			LOG_ERROR_IF_VAL_TRUE(ret, "add conn event fail....");

			memset(&attr, 0, sizeof(struct xio_connection_attr));
			attr.user_context = con;
			ARPC_LOG_NOTICE("##### new connection[%u][%p] create success.", con->id, con);
			xio_modify_connection(event_data->conn, &attr, XIO_CONNECTION_ATTR_USER_CTX);
			break;
		case XIO_SESSION_CONNECTION_TEARDOWN_EVENT:
			xio_connection_destroy(event_data->conn);
			if(event_data->conn_user_context != session_ctx->server){
				con = (struct arpc_connection *)event_data->conn_user_context;
				ARPC_LOG_ERROR("##### connection[%u][%p] teardown.", con->id, con);

				ret = arpc_del_tx_event_to_conn(con);
				LOG_ERROR_IF_VAL_TRUE(ret, "arpc_del_tx_event_to_conn fail....");

				ret = session_remove_con(session_fd, con);
				LOG_ERROR_IF_VAL_TRUE(ret, "session_remove_con fail.");
				ret = arpc_destroy_connection(con, 0);
				LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_connection fail.");
			}else{
				ARPC_LOG_NOTICE("connection[%p] is main thread, need tear down.", event_data->conn);
			}
			break;
		case XIO_SESSION_TEARDOWN_EVENT:
			xio_session_destroy(session);
			ARPC_LOG_ERROR("##### session[%p] teardown.", session_fd);
			ret = server_remove_session(session_ctx->server, session_fd);
			LOG_ERROR_IF_VAL_TRUE(ret, "server_insert_session fail.");
			if(session_ctx->server->session_teardown){
				ret = session_ctx->server->session_teardown(session_fd, 
															session_ctx->server->usr_context,
															session_fd->usr_context);
				LOG_ERROR_IF_VAL_TRUE(ret, "user session_teardown fail.");
			}
			ret = arpc_destroy_session(session_fd, 0);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_session fail.");
			break;
		case XIO_SESSION_ERROR_EVENT:
			break;
		default:
			break;
	};

	return 0;
}

int server_on_new_session(struct xio_session *session,struct xio_new_session_req *req, void *server_context)
{
	struct arpc_new_session_req client;
	struct arpc_con_info *ipv4;
	struct arpc_new_session_rsp param;
	struct arpc_session_handle *new_session = NULL;
	struct arpc_new_session_ctx *new_session_ctx = NULL;
	struct xio_session_attr attr;
	int ret;
	const char	**uri_vec;
	uint32_t i;
	QUEUE* work_q;
	struct arpc_work_handle *work_handle;
	struct arpc_server_handle *server_fd = (struct arpc_server_handle *)server_context;

	LOG_THEN_RETURN_VAL_IF_TRUE((!session || !req || !server_fd), -1, "invalid input.");

	memset(&client, 0, sizeof(struct arpc_new_session_req));
	memset(&param, 0, sizeof(param));

	param.rsp_data = NULL;
	param.rsp_data_len  = 0;
	param.ret_status = ARPC_E_STATUS_OK;

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!server_fd->new_session_start, reject, "new_session_start callback null.");
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!server_fd->new_session_end, reject, "new_session_end callback null.");

	ipv4 = &client.client_con_info;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((req->proto != XIO_PROTO_TCP), reject, "no tcp con, fail.");
	ipv4->type =ARPC_E_TRANS_TCP;
	arpc_get_ipv4_addr(&req->src_addr, ipv4->ipv4.ip, IPV4_MAX_LEN, &ipv4->ipv4.port);

	client.client_data.data = req->private_data;
	client.client_data.len = req->private_data_len;

	ret = server_fd->new_session_start(&client, &param, server_fd->usr_context);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ret != ARPC_SUCCESS), reject, "new_session_start return fail.");

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((param.ret_status != ARPC_E_STATUS_OK), reject, "ret_status[%d] not ok.", param.ret_status);

	// 尝试新建session
	new_session = arpc_create_session(SESSION_SERVER_CHILD, sizeof(struct arpc_new_session_ctx));
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!new_session, reject, "arpc_create_session null.");
	new_session->xio_s = session;
	new_session_ctx = (struct arpc_new_session_ctx *)new_session->ex_ctx;
	new_session_ctx->server = server_fd;
	if (param.ops_new_ctx)
		new_session->usr_context = param.ops_new_ctx;
	else
		new_session->usr_context = server_fd->usr_context;
	
	if (param.ops) {
		new_session->ops = *(param.ops);
	}else{
		new_session->ops = server_fd->ops;
	}

	new_session->msg_data_max_len = server_fd->msg_data_max_len;
	new_session->msg_head_max_len = server_fd->msg_head_max_len;
	new_session->msg_iov_max_len = server_fd->msg_iov_max_len;

	attr.ses_ops = NULL;
	attr.uri = NULL;
	attr.user_context = (void*)new_session;

	ret = xio_modify_session(session, &attr, XIO_SESSION_ATTR_USER_CTX);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ret != ARPC_SUCCESS), reject, "xio_modify_session fail.");

	if(server_fd->work_num) {
		uri_vec = ARPC_MEM_ALLOC(server_fd->work_num * sizeof(char *), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!uri_vec, reject, "ARPC_MEM_ALLOC fail.");
		i = 0;
		QUEUE_FOREACH_VAL(&server_fd->q_work, work_q, 
		{
			if (i < server_fd->work_num) { 
				work_handle = QUEUE_DATA(work_q, struct arpc_work_handle, q);
				uri_vec[i] = work_handle->uri;
				ARPC_LOG_NOTICE("work uri:%s.", uri_vec[i]);
				i++;
				continue;
			}
			break;
		});

		xio_accept(session, uri_vec, server_fd->work_num, param.rsp_data, param.rsp_data_len); 
		ARPC_MEM_FREE(uri_vec, NULL);
		uri_vec = NULL;
	}else{
		xio_accept(session, NULL, 0, param.rsp_data, param.rsp_data_len); 
	}

	ret = server_insert_session(server_fd, new_session);
	LOG_ERROR_IF_VAL_TRUE(ret, "server_insert_session fail.");
	server_fd->new_session_end((arpc_session_handle_t)new_session, &param, server_fd->usr_context);
	ARPC_LOG_NOTICE("create new session[%p] success, client[%s:%u].", server_fd, ipv4->ipv4.ip, ipv4->ipv4.port);
	return 0;
reject:
	if(new_session)
		arpc_destroy_session(new_session, 0);
	new_session = NULL;
	xio_reject(session, XIO_E_SESSION_ABORTED, param.rsp_data, param.rsp_data_len); // 拒绝session请求
	server_fd->new_session_end(NULL, &param, server_fd->usr_context);
	return -1;
}


int client_session_established(struct xio_session *session, struct xio_new_session_rsp *rsp, void *conn_context)
{
	return 0;
}

int client_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *session_context)
{
	int ret = 0;
	struct arpc_connection *con_ctx = NULL;
	struct xio_connection_params xio_con_param;
	SESSION_CTX(session_ctx, session_context);

	con_ctx = (struct arpc_connection *)event_data->conn_user_context;
	ARPC_LOG_DEBUG("#### event:%d|%s. reason: %s.", event_data->event,
					xio_session_event_str(event_data->event), 
					xio_strerror(event_data->reason));
	
	switch (event_data->event) {
		case XIO_SESSION_TEARDOWN_EVENT:
			xio_session_destroy(session);
			arpc_cond_lock(&session_ctx->cond);
			if (session_ctx->is_close) {
				ARPC_LOG_NOTICE(" user to tear down session.");
				arpc_cond_notify_all(&session_ctx->cond);
				arpc_cond_unlock(&session_ctx->cond);
				break;
			}
			arpc_cond_unlock(&session_ctx->cond);
			ARPC_LOG_NOTICE(" rebuild session!!!!!!!!!!.");
			ret = session_rebuild_for_client(session_ctx);
			LOG_ERROR_IF_VAL_TRUE(ret, "session_rebuild_for_client fail.");
			break;
		case XIO_SESSION_CONNECTION_ESTABLISHED_EVENT:
			arpc_cond_lock(&con_ctx->cond);
			if (event_data->conn != con_ctx->xio_con){
				xio_connection_destroy(con_ctx->xio_con);
				ARPC_LOG_ERROR("event_data->conn[%p], conn[%u][%p] not match!.", event_data->conn, con_ctx->id, con_ctx->xio_con);
				con_ctx->xio_con = event_data->conn;
			}

			ret = arpc_add_tx_event_to_conn(con_ctx);
			LOG_ERROR_IF_VAL_TRUE(ret, "add conn event fail....");

			con_ctx->status = XIO_STA_RUN_ACTION;
			con_ctx->client.recon_interval_s = 0;
			con_ctx->is_busy = 0;
			arpc_cond_notify(&con_ctx->cond);
			ARPC_LOG_NOTICE(" build connection[%u][%p] success!.", con_ctx->id, event_data->conn);
			arpc_cond_unlock(&con_ctx->cond);
			// 通知session
			arpc_cond_lock(&session_ctx->cond);
			arpc_cond_notify_all(&session_ctx->cond);
			arpc_cond_unlock(&session_ctx->cond);
			break;
		case XIO_SESSION_CONNECTION_TEARDOWN_EVENT: // conn断开，需要释放con资源
			if (event_data->conn)
				xio_connection_destroy(event_data->conn);
			ret = arpc_cond_lock(&con_ctx->cond);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_lock fail....");

			ret = arpc_del_tx_event_to_conn(con_ctx);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_del_tx_event_to_conn fail....");

			if (con_ctx->status == XIO_STA_CLEANUP) {
				ARPC_LOG_NOTICE("connection[%u][%p] tear down!.", con_ctx->id, event_data->conn);
				con_ctx->xio_con = NULL;
			}else{
				ARPC_LOG_ERROR("some error, connection[%u][%p] tear down!.", con_ctx->id, event_data->conn);
				(void)memset(&xio_con_param, 0, sizeof(struct xio_connection_params));
				xio_con_param.session			= session_ctx->xio_s;
				xio_con_param.ctx				= con_ctx->xio_con_ctx;
				xio_con_param.conn_idx			= con_ctx->client.affinity;
				xio_con_param.conn_user_context = con_ctx;
				con_ctx->xio_con = xio_connect(&xio_con_param);
				con_ctx->status = XIO_STA_TEARDOWN;
				con_ctx->client.recon_interval_s += 10;
			}
			arpc_cond_notify_all(&con_ctx->cond);
			arpc_cond_unlock(&con_ctx->cond);
			// 通知session
			arpc_cond_lock(&session_ctx->cond);
			arpc_cond_notify_all(&session_ctx->cond);
			arpc_cond_unlock(&session_ctx->cond);

			break;
		case XIO_SESSION_REJECT_EVENT:
		case XIO_SESSION_CONNECTION_REFUSED_EVENT: /**< connection refused event*/
			arpc_cond_lock(&con_ctx->cond);
			if (event_data->conn)
				xio_connection_destroy(event_data->conn);
			con_ctx->status = XIO_STA_TEARDOWN;
			con_ctx->client.recon_interval_s += 10;
			con_ctx->is_busy = 1;
			arpc_cond_notify(&con_ctx->cond);
			ARPC_LOG_NOTICE(" build connection[%u][%p] refused!.", con_ctx->id, event_data->conn);
			arpc_cond_unlock(&con_ctx->cond);
			// 通知session
			arpc_cond_lock(&session_ctx->cond);
			arpc_cond_notify_all(&session_ctx->cond);
			arpc_cond_unlock(&session_ctx->cond);
			break;
		case XIO_SESSION_ERROR_EVENT:
			break;
		default:
			break;
	};
	
	return ret;
}

int msg_head_process(struct xio_session *session, struct xio_msg *msg, void *conn_context)
{
	int ret = 0;
	SESSION_CONN_OPS_CTX(conn, conn_ops, conn_context);
	ARPC_LOG_DEBUG("header message type:%d", msg->type);
	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			ret = process_request_header(conn, msg, &conn_ops->req_ops, IOV_DEFAULT_MAX_LEN, conn->usr_ops_ctx);
			break;
		case XIO_MSG_TYPE_RSP:
			ret = process_rsp_header(msg, conn);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			ret = process_oneway_header(msg, &conn_ops->oneway_ops, IOV_DEFAULT_MAX_LEN, conn->usr_ops_ctx);
			break;  
		default:
			break;
	}
	return ret;
}

int msg_data_process(struct xio_session *session,struct xio_msg *msg, int last_in_rxq, void *conn_context)
{
	int ret = 0;
	SESSION_CONN_OPS_CTX(conn, conn_ops, conn_context);

	ARPC_LOG_DEBUG("msg_data_dispatch, msg type:%d", msg->type);

	ret = check_xio_msg_valid(conn, &msg->in);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "check_xio_msg_valid fail.");

	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			ret = process_request_data(conn, msg, &conn_ops->req_ops, last_in_rxq, conn->usr_ops_ctx);
			break;
		case XIO_MSG_TYPE_RSP:
			ret = process_rsp_data(msg, last_in_rxq, conn);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			//ret = set_connection_rx_mode(conn);
			//LOG_ERROR_IF_VAL_TRUE(ret, "set_connection_rx_mode fail.");
			ret = process_oneway_data(msg, &conn_ops->oneway_ops, last_in_rxq, conn->usr_ops_ctx);
			break;
		default:
			break;
	}

	return ret;
}

int msg_delivered(struct xio_session *session,struct xio_msg *msg, int last_in_rxq, void *conn_context)
{
	return 0;
}

int response_send_complete(struct xio_session *session,struct xio_msg *rsp, void *conn_context)
{
	int ret;
	struct arpc_common_msg *com_msg;
	SESSION_CONN_CTX(conn, conn_context);

	com_msg = (struct arpc_common_msg *)(rsp->user_context);

	ret = arpc_session_send_comp_notify(conn, com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_session_send_comp_notify fail.");

	ret = arpc_send_response_complete(com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_send_response clean up fail.");

	return ret;
}

static int on_msg_delivered(struct xio_session *session,
				struct xio_msg *msg,
				int last_in_rxq,
				void *conn_context)
{
	int ret;
	struct arpc_common_msg *com_msg;
	SESSION_CONN_CTX(conn, conn_context);

	com_msg = (struct arpc_common_msg *)(msg->user_context);

	ret = arpc_session_send_comp_notify(conn, com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_session_send_comp_notify fail.");

	if(msg->type == XIO_MSG_TYPE_ONE_WAY){
		ret = arpc_oneway_send_complete(com_msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_send_response clean up fail.");
	}
	
	return ret;
}

int oneway_send_complete(struct xio_session *session, struct xio_msg *msg, void *conn_context)
{
	int ret;
	struct arpc_common_msg *com_msg;
	SESSION_CONN_CTX(conn, conn_context);

	com_msg = (struct arpc_common_msg *)(msg->user_context);

	ret = arpc_session_send_comp_notify(conn, com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_session_send_comp_notify fail.");

	ret = arpc_oneway_send_complete(com_msg);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_send_response clean up fail.");

	return ret;
}

int message_error(struct xio_session *session, enum xio_status error, enum xio_msg_direction dir, struct xio_msg  *rsp, void *conn_context)
{
	int ret;
	struct arpc_common_msg *com_msg;
	SESSION_CONN_CTX(conn, conn_context);

	com_msg = (struct arpc_common_msg *)(rsp->user_context);
	ARPC_LOG_ERROR("msg_error, dir:%d, err:%d, error:%s. ", dir, error, xio_strerror(error));
	switch(dir) {
		case XIO_MSG_DIRECTION_OUT:
			ret = arpc_session_send_comp_notify(conn, com_msg);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_session_send_comp_notify fail.");
			if(rsp->type == XIO_MSG_TYPE_ONE_WAY){
				arpc_oneway_send_complete(com_msg);
			}else if(rsp->type == XIO_MSG_TYPE_RSP) {
				arpc_send_response_complete(com_msg);
			}
			break;
		case XIO_MSG_DIRECTION_IN:
			if(rsp->type == XIO_MSG_TYPE_RSP){
				xio_release_response(rsp);
			}
			break; 
		default:
			break;
	}
	return 0;
}