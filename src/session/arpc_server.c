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

#ifdef 	_DEF_SESSION_SERVER


#define GET_SERVER_SESSION_DATA(server_data, head, fd)	\
struct arpc_server_session_data *server_data = NULL;\
struct arpc_handle_ex *head = (struct arpc_handle_ex *)fd;\
do{\
	if(head && head->handle_ex)\
		server_data = (struct arpc_server_session_data *)head->handle_ex;\
	else\
		return -1;\
}while(0);

#define GET_NEW_SESSION_FD(new, head, ctx)	\
struct _arpc_new_client_session_data *new = NULL;\
struct arpc_handle_ex *head = (struct arpc_handle_ex *)ctx;\
do{\
	if(head && head->handle_ex)\
		new = (struct _arpc_new_client_session_data *)head->handle_ex;\
	else\
		return -1;\
}while(0);

/* ################## enum ###################*/

struct arpc_server_session_data {
	struct xio_server			*server;
	int (*new_session_start)(const struct arpc_new_session_req *, struct arpc_new_session_rsp *, void*);
	int (*new_session_end)(arpc_session_handle_t, struct arpc_new_session_rsp *, void*);
	struct arpc_session_ops	default_ops;
	uint32_t 					iov_max_len;
};

struct _arpc_new_client_session_data{
	struct arpc_session_ops	ops;
};

static int _msg_error(struct xio_session *session,
			    enum xio_status error,
			    enum xio_msg_direction dir,
			    struct xio_msg  *rsp,
			    void *cb_user_context)
{
	ARPC_LOG_NOTICE("msg_error message. ");
	return 0;
}
static int _server_session_event(struct xio_session *session,
			    struct xio_session_event_data *event_data,
			    void *cb_user_context)
{
	struct arpc_handle_ex *head = (struct arpc_handle_ex *)cb_user_context;
	struct xio_connection_attr attr;
	ARPC_LOG_DEBUG("##### session event:%d ,%s. reason: %s.",event_data->event,
					xio_session_event_str(event_data->event),
	       			xio_strerror(event_data->reason));
	switch (event_data->event) {
		case XIO_SESSION_NEW_CONNECTION_EVENT:	//新的链路
			if(head){
				head->active_conn = event_data->conn;
				attr.user_context = cb_user_context;
				xio_modify_connection(head->active_conn, &attr, XIO_CONNECTION_ATTR_USER_CTX);
			}
			head->status = SESSION_STA_RUN_ACTION;
			break;
		case XIO_SESSION_CONNECTION_TEARDOWN_EVENT:
			if (event_data->conn){
				 xio_connection_destroy(event_data->conn);
				head->active_conn =NULL;
				head->status = SESSION_STA_WAIT;
			}
			break;
		case XIO_SESSION_TEARDOWN_EVENT:
			if (session)
				xio_session_destroy(session);
			if (head && head->type == SESSION_SERVER_CHILD){
				pthread_mutex_destroy(&head->lock);
				ARPC_MEM_FREE(head, NULL);
			}
			break;
		case XIO_SESSION_ERROR_EVENT:
			break;
		default:
			break;
	};

	return 0;
}

static int _on_new_session(struct xio_session *session,
			  struct xio_new_session_req *req,
			  void *cb_user_context)
{
	struct arpc_new_session_req client;
	struct arpc_con_info *ipv4;
	struct arpc_new_session_rsp param;
	struct arpc_handle_ex *client_fd = NULL;
	struct _arpc_new_client_session_data *fd_ex = NULL;
	struct xio_session_attr attr;
	int ret;
	GET_SERVER_SESSION_DATA(server, head, cb_user_context);

	LOG_THEN_RETURN_VAL_IF_TRUE((!session || !req || !cb_user_context), -1, "invalid input.");

	memset(&client, 0, sizeof(struct arpc_new_session_req));
	memset(&param, 0, sizeof(param));

	param.rsp_data = NULL;
	param.rsp_data_len  = 0;
	param.ret_status = ARPC_E_STATUS_OK;

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!(server->new_session_start && server->new_session_end)), reject, "new_session callback null.");

	ipv4 = &client.client_con_info;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((req->proto != XIO_PROTO_TCP), reject, "no tcp con, fail.");
	ipv4->type =ARPC_E_TRANS_TCP;
	_arpc_get_ipv4_addr(&req->src_addr, ipv4->ipv4.ip, IPV4_MAX_LEN, &ipv4->ipv4.port);

	client.client_data.data = req->private_data;
	client.client_data.len = req->private_data_len;

	ret = server->new_session_start(&client, &param, head->usr_context);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ret != ARPC_SUCCESS), reject, "new_session return fail.");

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((param.ret_status != ARPC_E_STATUS_OK), reject, "ops null.");

	client_fd = ARPC_MEM_ALLOC(sizeof(struct arpc_handle_ex) + sizeof(struct _arpc_new_client_session_data), NULL);
	memset(client_fd, 0, sizeof(struct _arpc_new_client_session_data) + sizeof(struct _arpc_new_client_session_data));
	fd_ex = (struct _arpc_new_client_session_data *)client_fd->handle_ex;
	if (param.ops) {
		fd_ex->ops = *(param.ops);
	}else{
		fd_ex->ops = server->default_ops;
	}

	if (param.ops_new_ctx)
		client_fd->usr_context = param.ops_new_ctx;
	else
		client_fd->usr_context = head->usr_context;
	
	client_fd->type = SESSION_SERVER_CHILD; 
	client_fd->status = SESSION_STA_RUN_ACTION;
	
	attr.ses_ops = NULL;
	attr.uri = NULL;
	attr.user_context = (void*)client_fd;

	ret = xio_modify_session(session, &attr, XIO_SESSION_ATTR_USER_CTX);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ret != ARPC_SUCCESS), reject, "xio_modify_session fail.");

	pthread_mutex_init(&client_fd->lock, NULL);  /* 初始化互斥锁 */

	server->new_session_end((arpc_session_handle_t)client_fd, &param, head->usr_context);
	xio_accept(session, NULL, 0, param.rsp_data, param.rsp_data_len); 
	return 0;
reject:
	if(client_fd)
		ARPC_MEM_FREE(client_fd, NULL);
	client_fd = NULL;
	xio_reject(session, XIO_E_SESSION_ABORTED, param.rsp_data, param.rsp_data_len); // 拒绝session请求
	server->new_session_end(NULL, &param, head->usr_context);
	return -1;
}

static int _rsp_send_complete(struct xio_session *session,
				    struct xio_msg *rsp,
				    void *conn_user_context)
{
	GET_NEW_SESSION_FD(new_fd, head, conn_user_context);
	return _process_send_rsp_complete(rsp, &new_fd->ops.req_ops, head->usr_context);
}

static int _oneway_send_complete(struct xio_session *session,
				    struct xio_msg *rsp,
				    void *conn_user_context)
{
	struct arpc_msg  *_msg = (struct arpc_msg *)rsp->user_context;
	LOG_THEN_RETURN_VAL_IF_TRUE((!_msg), -1, "arpc_msg_data is empty.");
	return _process_send_complete(_msg);
}

static int _server_msg_header_dispatch(struct xio_session *session,
			  struct xio_msg *msg,
			  void *cb_user_context)
{
	int ret = 0;
	GET_NEW_SESSION_FD(fd, head, cb_user_context);
	ARPC_LOG_DEBUG("header message type:%d", msg->type);
	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			ret = _process_request_header(msg, &fd->ops.req_ops, IOV_DEFAULT_MAX_LEN, head->usr_context);
			break; 
		case XIO_MSG_TYPE_RSP:
			ret = _process_rsp_header(msg, head->usr_context);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			ret = _process_oneway_header(msg, &fd->ops.oneway_ops, IOV_DEFAULT_MAX_LEN, head->usr_context);
			break;
		default:
			break;
	}
	return ret;
}

static int _server_msg_data_dispatch(struct xio_session *session,
		       struct xio_msg *rsp,
		       int last_in_rxq,
		       void *cb_user_context)
{
	int ret = 0;
	GET_NEW_SESSION_FD(fd, head, cb_user_context);

	ARPC_LOG_DEBUG("message data type:%d", rsp->type);
	switch(rsp->type) {
		case XIO_MSG_TYPE_REQ:
			ret = _process_request_data(rsp, &fd->ops.req_ops, last_in_rxq, head->usr_context);
			break;
		case XIO_MSG_TYPE_RSP:
			ret = _process_rsp_data(rsp, last_in_rxq);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			ret = _process_oneway_data(rsp, &fd->ops.oneway_ops, last_in_rxq, head->usr_context);
			break;
		default:
			break;
	}
	
	return ret;
}
static struct xio_session_ops x_server_ops = {
	.on_session_event			=  &_server_session_event,
	.on_new_session				=  &_on_new_session,
	.rev_msg_data_alloc_buf		=  &_server_msg_header_dispatch,
	.on_msg						=  &_server_msg_data_dispatch,
	.on_msg_send_complete		=  &_rsp_send_complete,
	.on_ow_msg_send_complete	=  &_oneway_send_complete,
	.on_msg_error				=  &_msg_error
};

arpc_server_t arpc_server_create(const struct arpc_server_param *param)
{
	int ret = 0;
	struct arpc_server_session_data *x_data = NULL;
	struct arpc_handle_ex *fd = NULL;
	struct request_ops			*req_ops;
	/* handle*/
	fd = (struct arpc_handle_ex *)ARPC_MEM_ALLOC(sizeof(struct arpc_handle_ex) +
											sizeof(struct arpc_server_session_data),
											NULL);
	if (!fd) {
		ARPC_LOG_ERROR( "malloc error, exit ");
		return NULL;
	}
	fd->type = SESSION_SERVER;
	x_data = (struct arpc_server_session_data *)fd->handle_ex;

	x_data->new_session_start = param->new_session_start;
	x_data->new_session_end = param->new_session_end;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!x_data->new_session_start || !x_data->new_session_end) ,error_1, "new_session is null.");

	x_data->default_ops = param->default_ops;
	req_ops = &x_data->default_ops.req_ops;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!req_ops->proc_head_cb || !req_ops->proc_data_cb), error_1, "proc_data_cb is null.");

	x_data->iov_max_len = (param->iov_max_len > 512)?param->iov_max_len:IOV_DEFAULT_MAX_LEN;

	pthread_mutex_init(&fd->lock, NULL); /* 初始化互斥锁 */
    pthread_cond_init(&fd->cond, NULL);	/* 初始化条件变量 */

	ret = get_uri(&param->con, fd->uri, URI_MAX_LEN);
	if ( ret < 0) {
		ARPC_LOG_ERROR( "get_uri error, exit ");
		goto error_2;
	}

	/* context */
	fd->ctx = xio_context_create(NULL, 0, fd->affinity);
	if (fd->ctx == NULL){
		ARPC_LOG_ERROR( "xio_context_create error, exit ");
		goto error_3;
	}

	x_data->server = xio_bind(fd->ctx, &x_server_ops, fd->uri, NULL, 0, fd);
	if (!x_data->server){
		ARPC_LOG_ERROR( "xio_bind error, exit ");
		goto error_4;
	}

	fd->usr_context = param->default_ops_usr_ctx;
	fd->active_conn = NULL;
	fd->status 		= SESSION_STA_INIT;
	
	ARPC_LOG_NOTICE("Create server success.");

	return (arpc_server_t)fd;

error_4:
	if (x_data->server)
		xio_unbind(x_data->server);
	x_data->server = NULL;
error_3:
	if (fd->ctx) 
		xio_context_destroy(fd->ctx);
	fd->ctx = NULL;

error_2:
	pthread_cond_destroy(&fd->cond);
	pthread_mutex_destroy(&fd->lock);
error_1:
	if (fd)
		free(fd);
	ARPC_LOG_NOTICE( "destroy session success, exit.");
	return NULL;
}

int arpc_server_destroy(arpc_server_t *fd)
{
	struct arpc_server_session_data *x_data = NULL;
	struct arpc_handle_ex *_fd = NULL;
	if(!fd){
		return ARPC_ERROR;
	}
	_fd = (struct arpc_handle_ex *)*fd;
	if(!_fd){
		return ARPC_ERROR;
	}
	x_data = (struct arpc_server_session_data *)_fd->handle_ex;
	pthread_mutex_lock(&_fd->lock);
	if (x_data && x_data->server)
		xio_unbind(x_data->server);
	x_data->server = NULL;

	if (_fd->ctx) 
		xio_context_destroy(_fd->ctx);
	_fd->ctx = NULL;

	pthread_cond_destroy(&_fd->cond);

	pthread_mutex_unlock(&_fd->lock);
	pthread_mutex_destroy(&_fd->lock);

	if (_fd)
		ARPC_MEM_FREE(_fd, NULL);

	ARPC_LOG_NOTICE( "destroy session success, exit.");
	*fd = NULL;
	return ARPC_SUCCESS;
}

int arpc_server_loop(arpc_server_t fd, int32_t timeout_ms)
{
	struct arpc_handle_ex *_fd = (struct arpc_handle_ex *)fd;
	int32_t sleep_time = 0;
	if (!_fd){
		ARPC_LOG_ERROR( "fd null, exit.");
		return ARPC_ERROR;
	}

	ARPC_LOG_NOTICE("session run on the thread[%lu].", pthread_self());
	pthread_mutex_lock(&_fd->lock);
	_fd->status = SESSION_STA_RUN;
	pthread_mutex_unlock(&_fd->lock);

	for(;;){	
		if (xio_context_run_loop(_fd->ctx, timeout_ms) < 0)
			ARPC_LOG_NOTICE("xio error msg: %s.", xio_strerror(xio_errno()));

		ARPC_LOG_NOTICE("xio context run loop pause...");
		pthread_mutex_lock(&_fd->lock);
		sleep_time = _fd->reconn_interval_s;
		if(_fd->status == SESSION_STA_CLEANUP || timeout_ms > 0){
			ARPC_LOG_NOTICE("session ctx[%p] thread is stop loop, exit.", _fd->ctx);
			_fd->status = SESSION_STA_INIT;		// 状态恢复
			pthread_cond_broadcast(&_fd->cond); // 释放信号,让等待信号的线程退出
			pthread_mutex_unlock(&_fd->lock);
			break;
		}
		pthread_mutex_unlock(&_fd->lock);

		if (sleep_time > 0) 
			sleep(sleep_time);	// 恢复周期
	}

	ARPC_LOG_NOTICE("exit signaled.");
	return ARPC_SUCCESS;
}

#endif
