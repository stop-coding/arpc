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
#include <sys/time.h>

#include "base_log.h"
#include "queue.h"
#include "arpc_com.h"
#include "threadpool.h"

#ifdef 	_DEF_SESSION_CLIENT

#define RE_CREATE_SESSION(x_data)	\
do{\
	if (x_data->session)\
		xio_session_destroy(x_data->session);\
	x_data->session = NULL;\
	x_data->session = xio_session_create(&x_data->session_param);\
	x_data->conn_param.session = x_data->session;\
	if (!x_data->conn)\
		x_data->conn = xio_connect(&x_data->conn_param);\
}while(0);

#define GET_CLIENT_SESSION_DATA(client, head, fd)	\
struct arpc_client_session_data *client = NULL;\
struct arpc_handle_ex *head = (struct arpc_handle_ex *)fd;\
do{\
	if(head && head->handle_ex)\
		client = (struct arpc_client_session_data *)head->handle_ex;\
}while(0);

struct msg_hashtable {
	QUEUE queue;
};

/* ################## struct ###################*/
struct arpc_client_session_data {
	struct xio_session				*session; 		/* session 资源 */
	struct xio_connection			*conn;			/* connection 资源*/
	struct xio_session_params		session_param;
	struct xio_connection_params	conn_param;
	struct arpc_session_ops	    	*ops;
	struct msg_hashtable			msg_hash_table;			/*发送消息的队列*/
};

/* ################## function ###################*/

static int session_event(struct xio_session *session,
			    struct xio_session_event_data *event_data,
			    void *cb_user_context)
{
	GET_CLIENT_SESSION_DATA(x_data, fd, cb_user_context);

	ARPC_LOG_DEBUG("#### event:%d|%s. reason: %s, status:%d.",event_data->event,
					xio_session_event_str(event_data->event),
	       			xio_strerror(event_data->reason), fd->status);

	pthread_mutex_lock(&fd->lock);
	if (fd->status == SESSION_STA_INIT){
		 pthread_cond_signal(&fd->cond);
	}
	switch (event_data->event) {
		case XIO_SESSION_TEARDOWN_EVENT: //链路断开，立马重试
			fd->retry_time++;
			if(fd->status != SESSION_STA_CLEANUP){
				RE_CREATE_SESSION(x_data);
				ARPC_LOG_ERROR(" try build new session fd.");
			}else{
				if(session)
					xio_session_destroy(session);
				x_data->session = NULL;
			}
			xio_context_stop_loop(fd->ctx);
			break;
		case XIO_SESSION_CONNECTION_ESTABLISHED_EVENT:
			 fd->retry_time = 0;
			 fd->reconn_interval_s = 0;
			 fd->active_conn = event_data->conn;
			 fd->status = SESSION_STA_RUN_ACTION;
			 break;
		case XIO_SESSION_CONNECTION_TEARDOWN_EVENT: // conn断开，需要释放con资源
			if (event_data->conn)
				xio_connection_destroy(event_data->conn);
			x_data->conn = NULL;
			fd->active_conn = NULL;
			if (fd->status != SESSION_STA_CLEANUP)
				fd->status = SESSION_STA_WAIT;	// 断路
			break;
		case XIO_SESSION_REJECT_EVENT:
		case XIO_SESSION_CONNECTION_REFUSED_EVENT: /**< connection refused event*/
			if (fd->retry_time > RETRY_MAX_TIME) {
				fd->reconn_interval_s = SERVER_DOWN_WAIT_TIME;
				if (fd->status != SESSION_STA_CLEANUP)
					fd->status = SESSION_STA_WAIT;
			}else{
				fd->reconn_interval_s = 1;	//立马重连
				if (fd->status != SESSION_STA_CLEANUP)
					fd->status = SESSION_STA_RUN;
			}
			break;
		case XIO_SESSION_ERROR_EVENT:
			fd->retry_time++;
			break;
		default:
			break;
	};
	pthread_mutex_unlock(&fd->lock);
	return 0;
}

static int session_established(struct xio_session *session,
				      			struct xio_new_session_rsp *rsp,
				      			void *cb_user_context)
{
	ARPC_LOG_NOTICE("session established.");
	return 0;
}

static int msg_error(struct xio_session *session,
			    enum xio_status error,
			    enum xio_msg_direction dir,
			    struct xio_msg  *rsp,
			    void *cb_user_context)
{
	ARPC_LOG_ERROR("msg_error message to do. ");
	return 0;
}

static int _client_msg_header_dispatch(struct xio_session *session,
			  struct xio_msg *msg,
			  void *cb_user_context)
{
	int ret = 0;
	GET_CLIENT_SESSION_DATA(client, _fd, cb_user_context);

	ARPC_LOG_DEBUG("header message type:%d", msg->type);
	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			LOG_THEN_RETURN_VAL_IF_TRUE((!client->ops), -1, "client->ops is null.");
			ret = _process_request_header(msg, &client->ops->req_ops, IOV_DEFAULT_MAX_LEN, _fd->usr_context);
			break;
		case XIO_MSG_TYPE_RSP:
			ret = _process_rsp_header(msg, _fd->usr_context);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			LOG_THEN_RETURN_VAL_IF_TRUE((!client->ops), -1, "client->ops is null.");
			ret = _process_oneway_header(msg, &client->ops->oneway_ops, IOV_DEFAULT_MAX_LEN, _fd->usr_context);
			break;  
		default:
			break;
	}
	return ret;
}
static int _client_msg_data_dispatch(struct xio_session *session,
		       struct xio_msg *rsp,
		       int last_in_rxq,
		       void *cb_user_context)
{
	int ret = 0;
	GET_CLIENT_SESSION_DATA(client, _fd,cb_user_context);
	if (!rsp)
		return 0;
	ARPC_LOG_DEBUG("msg_data_dispatch, msg type:%d", rsp->type);
	switch(rsp->type) {
		case XIO_MSG_TYPE_REQ:
			LOG_THEN_RETURN_VAL_IF_TRUE((!client->ops), -1, "client->ops is null.");
			ret = _process_request_data(rsp, &client->ops->req_ops, last_in_rxq, _fd->usr_context);
			break;
		case XIO_MSG_TYPE_RSP:
			ret = _process_rsp_data(rsp, last_in_rxq);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			LOG_THEN_RETURN_VAL_IF_TRUE((!client->ops), -1, "client->ops is null.");
			ret = _process_oneway_data(rsp, &client->ops->oneway_ops, last_in_rxq, _fd->usr_context);
			break;
		default:
			break;
	}

	return ret;
}

static int _rsp_send_complete(struct xio_session *session,
				    struct xio_msg *rsp,
				    void *conn_user_context)
{
	GET_CLIENT_SESSION_DATA(client, _fd, conn_user_context);
	return _process_send_rsp_complete(rsp, &client->ops->req_ops, _fd->usr_context);
}

static int _on_msg_delivered(struct xio_session *session,
				struct xio_msg *msg,
				int last_in_rxq,
				void *conn_user_context)
{
	ARPC_LOG_DEBUG("_on_msg_delivered, msg type:%d", msg->type);
	return 0;
}

static int _ow_msg_send_complete(struct xio_session *session,
				       struct xio_msg *msg,
				       void *conn_user_context)
{
	struct arpc_msg  *_msg = (struct arpc_msg *)msg->user_context;
	LOG_THEN_RETURN_VAL_IF_TRUE((!_msg), -1, "arpc_msg_data is empty.");
	return _process_send_complete(_msg);
}

static struct xio_session_ops x_client_ops = {
	.on_session_event			=  &session_event,
	.on_session_established		=  &session_established,
	.rev_msg_data_alloc_buf		=  &_client_msg_header_dispatch,
	.on_msg						=  &_client_msg_data_dispatch,
	.on_msg_send_complete		=  &_rsp_send_complete,
	.on_msg_delivered			=  &_on_msg_delivered,
	.on_ow_msg_send_complete	=  &_ow_msg_send_complete,
	.on_msg_error				=  &msg_error
};

static int arpc_client_run_session(void * ctx)
{
	struct arpc_handle_ex *_fd = (struct arpc_handle_ex *)ctx;
	int32_t sleep_time = 0;
	if (!_fd){
		ARPC_LOG_ERROR( "fd null, exit.");
		return ARPC_ERROR;
	}

	ARPC_LOG_DEBUG("session run on the thread[%lu].", pthread_self());
	for(;;){	
		if (xio_context_run_loop(_fd->ctx, XIO_INFINITE) < 0)
			ARPC_LOG_ERROR("xio error msg: %s.", xio_strerror(xio_errno()));

		ARPC_LOG_DEBUG("xio context run loop pause...");
		pthread_mutex_lock(&_fd->lock);
		sleep_time = _fd->reconn_interval_s;
		if(_fd->status == SESSION_STA_CLEANUP){
			ARPC_LOG_DEBUG("session ctx[%p] thread is stop loop, exit.", _fd->ctx);
			pthread_cond_broadcast(&_fd->cond); // 释放信号,让等待信号的线程退出
			pthread_mutex_unlock(&_fd->lock);
			break;
		}
		pthread_mutex_unlock(&_fd->lock);

		if (sleep_time > 0) 
			sleep(sleep_time);	// 恢复周期
	}
	ARPC_LOG_DEBUG("exit signaled.");

	return ARPC_SUCCESS;
}

static void arpc_client_stop_session(void * ctx)
{
	arpc_client_destroy_session((arpc_session_handle_t)ctx);
	return;
}

arpc_session_handle_t arpc_client_create_session(const struct arpc_client_session_param *param)
{
	int ret = 0;
	work_handle_t hd;
	struct arpc_client_session_data *x_data = NULL;
	struct arpc_handle_ex *fd = NULL;
	struct tp_thread_work thread;
	/* handle*/
	fd = (struct arpc_handle_ex *)ARPC_MEM_ALLOC( sizeof(struct arpc_handle_ex) + 
												sizeof(struct arpc_client_session_data), 
												NULL);
	if (!fd) {
		ARPC_LOG_ERROR( "malloc error, exit ");
		return NULL;
	}
	memset(fd, 0, sizeof(struct arpc_handle_ex) + sizeof(struct arpc_client_session_data));

	fd->type = SESSION_CLIENT;
	x_data = (struct arpc_client_session_data *)fd->handle_ex;

	pthread_mutex_init(&fd->lock, NULL); /* 初始化互斥锁 */
    pthread_cond_init(&fd->cond, NULL);	 /* 初始化条件变量 */

	ret = get_uri(&param->con, fd->uri, URI_MAX_LEN);
	if ( ret < 0) {
		ARPC_LOG_ERROR( "get_uri error, exit ");
		goto error_2;
	}

	/* context */
	fd->ctx = xio_context_create(NULL, 0, fd->affinity);
	if (fd->ctx == NULL){
		ARPC_LOG_ERROR( "xio_context_create error, exit ");
		goto error_2;
	}

	/* session */
	(void)memset(&x_data->session_param, 0, sizeof(struct xio_session_params));
	x_data->session_param.type			= XIO_SESSION_CLIENT;
	x_data->session_param.ses_ops		= &x_client_ops;
	x_data->session_param.user_context	= fd;
	x_data->session_param.uri			= fd->uri;
	x_data->session_param.initial_sn	= 1;
	if (param->req_data && param->req_data_len && param->req_data_len < MAX_SESSION_REQ_DATA_LEN) {
		x_data->session_param.private_data = ARPC_MEM_ALLOC(param->req_data_len, NULL);
		if (x_data->session_param.private_data) {
			x_data->session_param.private_data_len = param->req_data_len;
			memcpy(x_data->session_param.private_data, param->req_data, param->req_data_len);
		}else{
			ARPC_LOG_ERROR( "ARPC_MEM_ALLOC private_data error, exit ");
			goto error_3;
		}
	}

	x_data->session = xio_session_create(&x_data->session_param);
	if (x_data->session == NULL){
		ARPC_LOG_ERROR( "xio_session_create error, exit ");
		goto error_3;
	}

	/* connection */
	(void)memset(&x_data->conn_param, 0, sizeof(struct xio_connection_params));
	x_data->conn_param.session			= x_data->session;
	x_data->conn_param.ctx				= fd->ctx;
	x_data->conn_param.conn_idx			= 0;
	x_data->conn_param.conn_user_context	= fd;
	x_data->conn = xio_connect(&x_data->conn_param);
	if(!x_data->conn){
		ARPC_LOG_ERROR( "xio_connect error, exit ");
		goto error_4;
	}
	/* others*/
	fd->usr_context = param->ops_usr_ctx;
	fd->active_conn = x_data->conn;
	fd->status 		= SESSION_STA_INIT;
	x_data->ops		= param->ops;

	/* 线程池申请资源*/
	thread.loop = &arpc_client_run_session;
	thread.stop = &arpc_client_stop_session;
	thread.usr_ctx = (void*)fd;
	hd = tp_post_one_work(_arpc_get_threadpool(), &thread, WORK_DONE_AUTO_FREE);
	if(!hd){
		ARPC_LOG_ERROR( "tp_post_one_work error, exit ");
		goto error_5;
	}
	pthread_mutex_lock(&fd->lock);
	pthread_cond_wait(&fd->cond, &fd->lock);
	pthread_mutex_unlock(&fd->lock);
	ARPC_LOG_DEBUG("Create session success.");
	return (arpc_session_handle_t)fd;

error_5:
	if(x_data->conn)
		xio_connection_destroy(x_data->conn);
	x_data->conn = NULL;

error_4:
	if(x_data->session)
		xio_session_destroy(x_data->session);
	x_data->session = NULL;
error_3:
	if (x_data->session_param.private_data) {
		ARPC_MEM_FREE(x_data->session_param.private_data, NULL);
	}
	x_data->session_param.private_data =NULL;
	if (fd->ctx) 
		xio_context_destroy(fd->ctx);
	fd->ctx = NULL;
error_2:
	pthread_cond_destroy(&fd->cond);
	pthread_mutex_destroy(&fd->lock);
	if (fd) 
		free(fd);
	fd = NULL;
	ARPC_LOG_ERROR( "create session fail, exit.");
	return NULL;
}

int arpc_client_destroy_session(arpc_session_handle_t *fd)
{
	struct arpc_client_session_data *x_data = NULL;
	struct arpc_handle_ex *_fd = (struct arpc_handle_ex *)*fd;
	if(!_fd){
		ARPC_LOG_ERROR( "fd is null.");
		return ARPC_ERROR;
	}
	pthread_mutex_lock(&_fd->lock);
	if(_fd->type != SESSION_CLIENT){
		ARPC_LOG_ERROR( "session not client session.");
		goto error;
	}

	if(_fd->status != SESSION_STA_CLEANUP) {
		ARPC_LOG_DEBUG( "session thread is running, status[%d].", _fd->status);
		_fd->status = SESSION_STA_CLEANUP; /* 停止 session*/
		if (_fd->active_conn)
			xio_disconnect(_fd->active_conn);
		_fd->active_conn = NULL;
		pthread_cond_wait(&_fd->cond, &_fd->lock); /* 等待退出的信号 */
	}

	x_data = (struct arpc_client_session_data *)_fd->handle_ex;
	if (x_data){
		if(x_data->session)
			xio_session_destroy(x_data->session);
		x_data->session = NULL;
	}

	if (x_data->session_param.private_data) {
		ARPC_MEM_FREE(x_data->session_param.private_data, NULL);
	}
	x_data->session_param.private_data =NULL;

	if (_fd->ctx) 
		xio_context_destroy(_fd->ctx);
	_fd->ctx = NULL;
	pthread_cond_destroy(&_fd->cond);

	pthread_mutex_unlock(&_fd->lock);
	pthread_mutex_destroy(&_fd->lock);

	if (_fd) 
		ARPC_MEM_FREE(_fd, NULL);
	*fd = NULL;
	ARPC_LOG_DEBUG( "destroy session success, exit.");
	return ARPC_SUCCESS;
error:
	 pthread_mutex_unlock(&_fd->lock);
	return ARPC_ERROR;
}

enum arpc_session_status arpc_get_session_status(const arpc_session_handle_t fd)
{
	struct arpc_handle_ex *_fd = (struct arpc_handle_ex *)fd;
	enum session_status			status;
	enum arpc_session_status	out_sta = ARPC_SESSION_STA_NOT_EXISTED;
	if(!_fd){
		return ARPC_ERROR;
	}

	status = _fd->status;
	switch (status)
	{
		case SESSION_STA_RUN:
			out_sta = ARPC_SESSION_STA_RE_CON;
			break;
		case SESSION_STA_RUN_ACTION:
			out_sta = ARPC_SESSION_STA_ACTIVE;
			break;
		case SESSION_STA_WAIT:
			out_sta = ARPC_SESSION_STA_WAIT;
			break;
		default:
			break;
	}
	return out_sta;
}
#endif
