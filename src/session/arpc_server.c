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

#include "arpc_com.h"
#include "arpc_session.h"
#include "arpc_make_request.h"

#ifdef 	_DEF_SESSION_SERVER

struct arpc_new_session_ctx{
	void *new_session_usr_ctx;
	struct arpc_server_handle *server;
	void *server_usr_ctx;
};

static int _ow_msg_send_complete(struct xio_session *session,
				    struct xio_msg *msg,
				    void *conn_user_context);
					
static int server_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *cb_user_context)
{
	struct xio_connection_attr attr;
	int ret;
	struct arpc_connection *con;
	struct arpc_work_handle *work;
	struct arpc_new_session_ctx *session_ctx;
	SESSION_CTX(session_fd, cb_user_context);

	ARPC_LOG_DEBUG("##### session event:%d ,%s. reason: %s.",event_data->event,
					xio_session_event_str(event_data->event),
	       			xio_strerror(event_data->reason));
	ARPC_LOG_DEBUG("##### session:%p ,event_data->conn_user_context:%p.",
					session_fd,
					event_data->conn_user_context);
	switch (event_data->event) {
		case XIO_SESSION_NEW_CONNECTION_EVENT:	//新的链路
			work = (struct arpc_work_handle *)event_data->conn_user_context;
			con = arpc_create_con(ARPC_CON_TYPE_SERVER, session_fd, 0);
			LOG_THEN_RETURN_VAL_IF_TRUE(!con, -1, "arpc_create_con fail.");
			con->xio_con = event_data->conn;
			con->server.work = work;
			memset(&attr, 0, sizeof(struct xio_connection_attr));
			attr.user_context = con;
			ARPC_LOG_NOTICE("##### new connection[%p] create success.", con);
			xio_modify_connection(event_data->conn, &attr, XIO_CONNECTION_ATTR_USER_CTX);
			break;
		case XIO_SESSION_CONNECTION_TEARDOWN_EVENT:
			xio_connection_destroy(event_data->conn);
			if(event_data->conn_user_context){
				con = (struct arpc_connection *)event_data->conn_user_context;
				ARPC_LOG_NOTICE("##### connection[%p] teardown.", con);
				ret = session_remove_con(session_fd, con);
				LOG_ERROR_IF_VAL_TRUE(ret, "session_remove_con fail.");
				ret = arpc_destroy_con(con);
				LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_con fail.");
			}
			break;
		case XIO_SESSION_TEARDOWN_EVENT:
			xio_session_destroy(session);
			ARPC_LOG_NOTICE("##### session[%p] teardown.", session_fd);
			session_ctx = (struct arpc_new_session_ctx *)session_fd->ex_ctx;
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

static int server_on_new_session(struct xio_session *session,struct xio_new_session_req *req, void *cb_server_context)
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
	struct arpc_server_handle *server_fd = (struct arpc_server_handle *)cb_server_context;

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
	_arpc_get_ipv4_addr(&req->src_addr, ipv4->ipv4.ip, IPV4_MAX_LEN, &ipv4->ipv4.port);

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

static int _msg_error(struct xio_session *session,
			    enum xio_status error,
			    enum xio_msg_direction dir,
			    struct xio_msg  *rsp,
			    void *conn_user_context)
{
	SESSION_CONN_CTX(conn, conn_user_context);
	ARPC_LOG_ERROR("#### msg_error message, dir:%d, err:%d. ", dir, error);
	switch(dir) {
		case XIO_MSG_DIRECTION_OUT:
			if(rsp->type == XIO_MSG_TYPE_REQ) {
				_arpc_rev_request_rsp(rsp);
			}else if(rsp->type == XIO_MSG_TYPE_ONE_WAY){
				_ow_msg_send_complete(session, rsp, conn_user_context);
			}else if(rsp->type == XIO_MSG_TYPE_RSP) {
				_process_send_rsp_complete(rsp, conn_user_context);
			}
			break;
		case XIO_MSG_DIRECTION_IN:
			break; 
		default:
			break;
	}
	return 0;
}

static int _rsp_send_complete(struct xio_session *session,
				    struct xio_msg *rsp,
				    void *conn_user_context)
{
	SESSION_CONN_CTX(conn, conn_user_context);
	return _process_send_rsp_complete(rsp, conn->usr_ops_ctx);
}

static int _ow_msg_send_complete(struct xio_session *session,
				    struct xio_msg *msg,
				    void *conn_user_context)
{
	struct arpc_conn_ow_msg *poneway_msg = (struct arpc_conn_ow_msg *)msg->user_context;
	SESSION_CONN_CTX(conn, conn_user_context);
	LOG_THEN_RETURN_VAL_IF_TRUE((!poneway_msg), -1, "poneway_msg is empty.");
	ARPC_LOG_NOTICE("_ow_msg_send_complete id:[%u], source addr[%p].", conn->id, poneway_msg);
	return _oneway_send_complete(poneway_msg, conn_user_context);
}

static int _server_msg_header_dispatch(struct xio_session *session,
			  struct xio_msg *msg,
			  void *conn_user_context)
{
	int ret = 0;
	SESSION_CONN_OPS_CTX(conn, conn_ops, conn_user_context);

	ARPC_LOG_DEBUG("header message type:%d", msg->type);
	ARPC_LOG_DEBUG("##### _server_msg_header_dispatch usr_ops_ctx:%p.", conn->usr_ops_ctx);
	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			ret = _process_request_header(msg, &conn_ops->req_ops, IOV_DEFAULT_MAX_LEN, conn->usr_ops_ctx);
			break; 
		case XIO_MSG_TYPE_RSP:
			ret = _process_rsp_header(msg, conn->usr_ops_ctx);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			ret = set_connection_mode(conn, ARPC_CON_MODE_DIRE_IN);
			LOG_ERROR_IF_VAL_TRUE(ret, "set_connection_rx_mode fail.");
			ret = _process_oneway_header(msg, &conn_ops->oneway_ops, IOV_DEFAULT_MAX_LEN, conn->usr_ops_ctx);
			break;
		default:
			break;
	}
	return ret;
}

static int _server_msg_data_dispatch(struct xio_session *session,
		       struct xio_msg *rsp,
		       int last_in_rxq,
		       void *conn_user_context)
{
	int ret = 0;
	SESSION_CONN_OPS_CTX(conn, conn_ops, conn_user_context);

	ARPC_LOG_DEBUG("message data type:%d", rsp->type);
	switch(rsp->type) {
		case XIO_MSG_TYPE_REQ:
			ret = _process_request_data(rsp, &conn_ops->req_ops, last_in_rxq, conn->usr_ops_ctx);
			break;
		case XIO_MSG_TYPE_RSP:
			ret = _process_rsp_data(rsp, last_in_rxq);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			ARPC_LOG_NOTICE("rx oneway msg connid:[%u].", conn->id);
			ret = _process_oneway_data(rsp, &conn_ops->oneway_ops, last_in_rxq, conn->usr_ops_ctx);
			ret = set_connection_mode(conn, ARPC_CON_MODE_DIRE_IO);
			LOG_ERROR_IF_VAL_TRUE(ret, "set_connection_rx_mode fail.");
			break;
		default:
			break;
	}
	
	return ret;
}
static int _on_msg_delivered(struct xio_session *session,
				struct xio_msg *msg,
				int last_in_rxq,
				void *conn_user_context)
{
	struct arpc_conn_ow_msg *poneway_msg = (struct arpc_conn_ow_msg *)msg->user_context;
	ARPC_LOG_DEBUG("************!!!!!!!!!_on_msg_delivered, msg type:%d", msg->type);
	LOG_THEN_RETURN_VAL_IF_TRUE((!poneway_msg), -1, "poneway_msg is empty.");
	return _oneway_send_complete(poneway_msg, conn_user_context);
}

static struct xio_session_ops x_server_ops = {
	.on_session_event			=  &server_session_event,
	.on_new_session				=  &server_on_new_session,
	.rev_msg_data_alloc_buf		=  &_server_msg_header_dispatch,
	.on_msg						=  &_server_msg_data_dispatch,
	.on_msg_delivered			=  &_on_msg_delivered,
	.on_msg_send_complete		=  &_rsp_send_complete,
	.on_ow_msg_send_complete	=  &_ow_msg_send_complete,
	.on_msg_error				=  &_msg_error
};

arpc_server_t arpc_server_create(const struct arpc_server_param *param)
{
	int ret = 0;
	struct arpc_server_handle *server = NULL;
	struct request_ops			*req_ops;
	int32_t i;
	struct arpc_work_handle *work_handle;
	struct arpc_con_info con_param;
	uint32_t work_num = 0;
	/* handle*/
	server = arpc_create_server(0);
	LOG_THEN_RETURN_VAL_IF_TRUE(!server, NULL, "arpc_create_server fail");
	
	server->new_session_start = param->new_session_start;
	server->new_session_end = param->new_session_end;
	server->session_teardown = param->session_teardown;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!server->new_session_start || !server->new_session_end) ,error_1, "new_session is null.");

	server->ops = param->default_ops;
	req_ops = &server->ops.req_ops;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!req_ops->proc_head_cb || !req_ops->proc_data_cb), error_1, "proc_data_cb is null.");

	server->iov_max_len = (param->iov_max_len > 512) ? param->iov_max_len:server->iov_max_len;

	con_param = param->con;

	ret = get_uri(&con_param, server->uri, URI_MAX_LEN);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, error_1, "arpc_create_server fail");
	work_num = tp_get_pool_idle_num(server->threadpool);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(work_num < ARPC_MIN_THREAD_IDLE_NUM, error_1, "idle thread num is low then min [%u]", ARPC_MIN_THREAD_IDLE_NUM);
	work_num = work_num - ARPC_MIN_THREAD_IDLE_NUM;
	work_num = (param->work_num && param->work_num <= work_num)? param->work_num : 2; // 默认只有1个主线程,2个工作线程
	for (i = 0; i < work_num; i++) {
		con_param.ipv4.port++; // 端口递增
		work_handle = arpc_create_xio_server_work(&con_param, server->threadpool, &x_server_ops, i);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!work_handle, error_1, "arpc_create_xio_server_work fail.");
		ret = server_insert_work(server, work_handle);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, error_2, "insert_queue fail.");
	}
	work_handle = NULL;
	/* context */
	server->server_ctx = xio_context_create(NULL, 0, 0);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!server->server_ctx, error_2, "xio_context_create fail");

	server->server = xio_bind(server->server_ctx, &x_server_ops, server->uri, NULL, 0, server);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!server->server_ctx, error_3, "xio_context_create fail");

	server->usr_context = param->default_ops_usr_ctx;

	ARPC_LOG_NOTICE("Create main server[%p] success, work server num[%u].", server, server->work_num);

	return (arpc_server_t)server;

error_3:
	if (server->server_ctx) 
		xio_context_destroy(server->server_ctx);
	server->server_ctx = NULL;
error_2:
	if (work_handle) 
		arpc_destroy_xio_server_work(work_handle);
	work_handle = NULL;
error_1:
	if (server)
		arpc_destroy_server(server);
	ARPC_LOG_NOTICE( "some error, destroy server, exit.");
	return NULL;
}

int arpc_server_destroy(arpc_server_t *fd)
{
	struct arpc_server_handle *server = NULL;

	LOG_THEN_RETURN_VAL_IF_TRUE(!fd, -1, "arpc_create_server fail");
	server = (struct arpc_server_handle *)(*fd);

	if (server->server_ctx) 
		xio_context_destroy(server->server_ctx);
	server->server_ctx = NULL;

	if (server)
		arpc_destroy_server(server);
	ARPC_LOG_NOTICE( "destroy server success, exit.");
	*fd = NULL;
	return ARPC_SUCCESS;
}

int arpc_server_loop(arpc_server_t fd, int32_t timeout_ms)
{
	struct arpc_server_handle *server = (struct arpc_server_handle *)fd;
	if (!server){
		ARPC_LOG_ERROR( "fd null, exit.");
		return ARPC_ERROR;
	}
	ARPC_LOG_NOTICE("server run on the thread[%lu].", pthread_self());
	for(;;){	
		if (xio_context_run_loop(server->server_ctx, timeout_ms) < 0)
			ARPC_LOG_NOTICE("xio error msg: %s.", xio_strerror(xio_errno()));
		ARPC_LOG_NOTICE("xio server stop loop pause...");
		break;
	}

	ARPC_LOG_NOTICE("server run thread[%lu] exit signaled.", pthread_self());
	return -1;
}

#endif
