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
#include "arpc_com.h"
#include "arpc_session.h"

#ifdef 	_DEF_SESSION_CLIENT


#define CLIENT_SESSION_CTX(ctx, session_fd, usr_ctx)	\
struct arpc_client_ctx *ctx = NULL;\
struct arpc_session_handle *session_fd = (struct arpc_session_handle *)usr_ctx;\
do{\
	if(session_fd && session_fd->ex_ctx)\
		ctx = (struct arpc_client_ctx *)session_fd->ex_ctx;\
}while(0);

static int _ow_msg_send_complete(struct xio_session *session, struct xio_msg *msg, void *conn_user_context);

/* ################## struct ###################*/
struct arpc_client_ctx {
	struct xio_session_params	xio_param;
	char 						uri[URI_MAX_LEN];
	void						*private_data;  /**< private user data snt to */
	uint64_t					private_data_len; /**< private data length    */
};

/* ################## function ###################*/

static int session_event(struct xio_session *session,
			    struct xio_session_event_data *event_data,
			    void *cb_user_context)
{
	int ret = 0;
	struct arpc_connection *con_ctx = NULL;
	SESSION_CTX(session_ctx, cb_user_context);

	con_ctx = (struct arpc_connection *)event_data->conn_user_context;
	ARPC_LOG_DEBUG("#### event:%d|%s. reason: %s.", event_data->event,
					xio_session_event_str(event_data->event), 
					xio_strerror(event_data->reason));
	
	switch (event_data->event) {
		case XIO_SESSION_TEARDOWN_EVENT:
			xio_session_destroy(session);
			arpc_mutex_lock(&session_ctx->lock);
			if (session_ctx->is_close) {
				ARPC_LOG_NOTICE(" user to tear down session.");
				arpc_mutex_unlock(&session_ctx->lock);
				arpc_cond_lock(&session_ctx->cond);
				arpc_cond_notify_all(&session_ctx->cond);
				arpc_cond_unlock(&session_ctx->cond);
				break;
			}
			arpc_mutex_unlock(&session_ctx->lock);
			rebuild_session(session_ctx);
			ARPC_LOG_NOTICE(" rebuild session!!!!!!!!!!.");
			break;
		case XIO_SESSION_CONNECTION_ESTABLISHED_EVENT:
			arpc_cond_lock(&con_ctx->cond);
			if (event_data->conn != con_ctx->xio_con)
				xio_connection_destroy(con_ctx->xio_con);
			con_ctx->xio_con = event_data->conn;
			con_ctx->status = XIO_STA_RUN_ACTION;
			con_ctx->client.recon_interval_s = 0;
			con_ctx->is_busy = 0;
			arpc_cond_notify(&con_ctx->cond);
			ARPC_LOG_NOTICE(" build connection[%u][%p] success!.", con_ctx->id, event_data->conn);
			arpc_cond_unlock(&con_ctx->cond);
			break;
		case XIO_SESSION_CONNECTION_TEARDOWN_EVENT: // conn断开，需要释放con资源
			ARPC_LOG_NOTICE("connection[%u][%p] tear down!.", con_ctx->id, event_data->conn);
			arpc_cond_lock(&con_ctx->cond);
			if (event_data->conn)
				xio_connection_destroy(event_data->conn);
			con_ctx->xio_con = NULL;
			con_ctx->status = XIO_STA_TEARDOWN;
			con_ctx->client.recon_interval_s += 10;
			con_ctx->is_busy = 1;
			arpc_cond_notify_all(&con_ctx->cond);
			arpc_cond_unlock(&con_ctx->cond);
			break;
		case XIO_SESSION_REJECT_EVENT:
		case XIO_SESSION_CONNECTION_REFUSED_EVENT: /**< connection refused event*/
			arpc_cond_lock(&con_ctx->cond);
			if (event_data->conn != con_ctx->xio_con)
				xio_connection_destroy(con_ctx->xio_con);
			con_ctx->status = XIO_STA_RUN;
			con_ctx->client.recon_interval_s += 10;
			con_ctx->is_busy = 1;
			arpc_cond_notify(&con_ctx->cond);
			ARPC_LOG_NOTICE(" build connection[%u][%p] refused!.", con_ctx->id, event_data->conn);
			arpc_cond_unlock(&con_ctx->cond);
			break;
		case XIO_SESSION_ERROR_EVENT:
			break;
		default:
			break;
	};
	
	return ret;
}

static int session_established(struct xio_session *session,
				      			struct xio_new_session_rsp *rsp,
				      			void *conn_user_context)
{
	ARPC_LOG_NOTICE("client session established, %p.", conn_user_context);
	return 0;
}

static int msg_error(struct xio_session *session,
			    enum xio_status error,
			    enum xio_msg_direction dir,
			    struct xio_msg  *rsp,
			    void *conn_user_context)
{
	int ret = 0;
	SESSION_CONN_CTX(conn, conn_user_context);
	ARPC_LOG_ERROR("msg_error message,dir:%d, err:%d. ", dir, error);
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

static int _client_msg_header_dispatch(struct xio_session *session, struct xio_msg *msg, void *conn_user_context)
{
	int ret = 0;
	SESSION_CONN_OPS_CTX(conn, conn_ops, conn_user_context);

	ARPC_LOG_DEBUG("header message type:%d", msg->type);
	switch(msg->type) {
		case XIO_MSG_TYPE_REQ:
			ret = _process_request_header(msg, &conn_ops->req_ops, IOV_DEFAULT_MAX_LEN, conn->usr_ops_ctx);
			break;
		case XIO_MSG_TYPE_RSP:
			ret = _process_rsp_header(msg, conn->usr_ops_ctx);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			ret = _process_oneway_header(msg, &conn_ops->oneway_ops, IOV_DEFAULT_MAX_LEN, conn->usr_ops_ctx);
			break;  
		default:
			break;
	}
	return ret;
}
static int _client_msg_data_dispatch(struct xio_session *session,
		       struct xio_msg *rsp,
		       int last_in_rxq,
		       void *conn_user_context)
{
	int ret = 0;
	SESSION_CONN_OPS_CTX(conn, conn_ops, conn_user_context);

	ARPC_LOG_DEBUG("msg_data_dispatch, msg type:%d", rsp->type);
	switch(rsp->type) {
		case XIO_MSG_TYPE_REQ:
			ret = _process_request_data(rsp, &conn_ops->req_ops, last_in_rxq, conn->usr_ops_ctx);
			break;
		case XIO_MSG_TYPE_RSP:
			ret = _process_rsp_data(rsp, last_in_rxq);
			break;
		case XIO_MSG_TYPE_ONE_WAY:
			//ret = set_connection_rx_mode(conn);
			//LOG_ERROR_IF_VAL_TRUE(ret, "set_connection_rx_mode fail.");
			ret = _process_oneway_data(rsp, &conn_ops->oneway_ops, last_in_rxq, conn->usr_ops_ctx);
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
	return _process_send_rsp_complete(rsp, conn_user_context);
}

static int _on_msg_delivered(struct xio_session *session,
				struct xio_msg *msg,
				int last_in_rxq,
				void *conn_user_context)
{
	struct arpc_send_one_way_msg *poneway_msg = (struct arpc_send_one_way_msg *)msg->user_context;
	ARPC_LOG_DEBUG("************!!!!!!!!!_on_msg_delivered, msg type:%d", msg->type);
	LOG_THEN_RETURN_VAL_IF_TRUE((!poneway_msg), -1, "poneway_msg is empty.");
	return _oneway_send_complete(poneway_msg, conn_user_context);
}

static int _ow_msg_send_complete(struct xio_session *session, struct xio_msg *msg, void *conn_user_context)
{
	struct arpc_send_one_way_msg *poneway_msg = (struct arpc_send_one_way_msg *)msg->user_context;
	LOG_THEN_RETURN_VAL_IF_TRUE((!poneway_msg), -1, "poneway_msg is empty.");
	ARPC_LOG_DEBUG("************!!!!!!!!!_ow_msg_send_complete, msg type:%d", msg->type);
	return _oneway_send_complete(poneway_msg, conn_user_context);
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

arpc_session_handle_t arpc_client_create_session(const struct arpc_client_session_param *param)
{
	int ret = 0;
	work_handle_t hd;
	int i =0;
	struct arpc_client_ctx *client_ctx = NULL;
	struct arpc_session_handle *session = NULL;
	struct tp_thread_work thread;
	uint32_t idle_thread_num;
	struct arpc_connection *con;
	struct xio_connection_params xio_con_param;
	uint32_t rx_con_num = 0;
	//LOG_THEN_RETURN_VAL_IF_TRUE(!param->ops, NULL, "ops is null.");

	session = arpc_create_session(SESSION_CLIENT, sizeof(struct arpc_client_ctx));
	LOG_THEN_RETURN_VAL_IF_TRUE(!session, NULL, "create client session handle fail.");

	client_ctx = (struct arpc_client_ctx *)session->ex_ctx;
	if(param->ops){
		session->ops = *(param->ops);
	}
	session->usr_context = param->ops_usr_ctx;
	ret = get_uri(&param->con, client_ctx->uri, URI_MAX_LEN);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, error_1, "get_uri fail.");

	client_ctx->xio_param.type			= XIO_SESSION_CLIENT;
	client_ctx->xio_param.ses_ops		= &x_client_ops;
	client_ctx->xio_param.user_context	= session;
	client_ctx->xio_param.uri			= client_ctx->uri;
	client_ctx->xio_param.initial_sn	= 1;
	if (param->req_data && param->req_data_len && param->req_data_len < MAX_SESSION_REQ_DATA_LEN) {
		client_ctx->private_data = ARPC_MEM_ALLOC(param->req_data_len, NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!client_ctx->private_data, error_1, "ARPC_MEM_ALLOC fail.");
		client_ctx->private_data_len = param->req_data_len;
		memcpy(client_ctx->private_data, param->req_data, param->req_data_len);
	}

	session->xio_s = xio_session_create(&client_ctx->xio_param);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!session->xio_s, error_2, "xio_session_create fail.");

	idle_thread_num = tp_get_pool_idle_num(session->threadpool);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(idle_thread_num < ARPC_MIN_THREAD_IDLE_NUM, error_2, "idle_thread_num[%u] is low 2.", idle_thread_num);

	ARPC_LOG_NOTICE("Max idle thread num[%u], user expact max num[%u].", idle_thread_num, param->con_num);
	idle_thread_num = idle_thread_num - ARPC_MIN_THREAD_IDLE_NUM;
	idle_thread_num = (param->con_num && param->con_num < idle_thread_num)? param->con_num : idle_thread_num; // 默认是两个链接

	idle_thread_num = (idle_thread_num < ARPC_CLIENT_MAX_CON_NUM)? idle_thread_num: ARPC_CLIENT_MAX_CON_NUM;

	rx_con_num = (param->rx_con_num > 1 )? param->rx_con_num: 1; //至少保留一个接收通道
	for (i = 0; i < idle_thread_num; i++) {
		con = arpc_create_con(ARPC_CON_TYPE_CLIENT, session, i);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!con, error_2, "arpc_create_xio_client_con fail.");
		ret = arpc_wait_connected(con, 10*1000);
		LOG_ERROR_IF_VAL_TRUE(ret, "wait_connection_finished timeout, continue.");
		if(rx_con_num){
			rx_con_num--;
			set_connection_mode(con, ARPC_CON_MODE_DIRE_IN);
			ARPC_LOG_NOTICE("connection[%u][%p] set rx mode!!", i, con);
		}
	}

	ARPC_LOG_NOTICE("Create session[%p] success, work thread num[%u]!!", session, idle_thread_num);

	return (arpc_session_handle_t)session;

error_2:
	SAFE_FREE_MEM(client_ctx->private_data);
error_1:
	arpc_destroy_session(session);
	ARPC_LOG_ERROR( "create session fail, exit.");
	return NULL;
}

int arpc_client_destroy_session(arpc_session_handle_t *fd)
{
	int ret;
	struct arpc_session_handle *session = NULL;
	LOG_THEN_RETURN_VAL_IF_TRUE(!fd, -1, "client session handle is null fail.");

	session = (struct arpc_session_handle *)(*fd);
	ret = arpc_destroy_session(session);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_destroy_session[%p] fail.", session);
	ARPC_LOG_NOTICE( "destroy session[%p] success.", session);
	*fd = NULL;
	return ret;
}

enum arpc_session_status arpc_get_session_status(const arpc_session_handle_t fd)
{
	return ARPC_SESSION_STA_ACTIVE;
}
#endif
