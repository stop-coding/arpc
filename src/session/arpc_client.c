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
#include <sys/prctl.h>

#include "base_log.h"
#include "arpc_com.h"
#include "arpc_session.h"
#include "arpc_request.h"
#include "arpc_response.h"
#include "arpc_xio_callback.h"
#include "arpc_proto.h"

#define CLIENT_DESTROY_SESSION_MAX_TIME (3*1000)

#define ARPC_CLIENT_LOOP_MIN_TIME_MS    (100)

static int client_session_established(struct xio_session *session, struct xio_new_session_rsp *rsp, void *session_context);
static int client_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *session_context);

static struct xio_session_ops x_client_ops = {
	.on_session_event			=  &client_session_event,
	.on_session_established		=  &client_session_established,
	.rev_msg_data_alloc_buf		=  &msg_head_process,
	.on_msg						=  &msg_data_process,
	.on_msg_send_complete		=  &response_send_complete,
	.on_msg_delivered			=  &msg_delivered,
	.on_ow_msg_send_complete	=  &oneway_send_complete,
	.on_msg_error				=  &message_error
};

static int arpc_client_deamon(void *thread_ctx)
{
	struct arpc_session_handle *session = (struct arpc_session_handle *)thread_ctx;
	//cpu_set_t		cpuset;
	QUEUE* iter;
	int ret;
	char thread_name[16+1] ={0};
	prctl(PR_SET_NAME, "client_deamon");

	ret = arpc_cond_lock(&session->deamon_cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session[%p] fail", session);

	while(!session->is_close){
		arpc_cond_wait_timeout(&session->deamon_cond, 15*1000);
		if (session->is_close) {
			break;
		}
		arpc_update_log_status();
		if(ret) {
			ARPC_LOG_ERROR("deamon lock client[%p] fail, status:%d,.", session, session->status);
			break;
		}
		print_session_status(session, NULL);
	}
	arpc_cond_unlock(&session->deamon_cond);
	ARPC_LOG_NOTICE("deamon exit[%lu].", pthread_self());
	return 0;
} 

arpc_session_handle_t arpc_client_create_session(const struct arpc_client_session_param *param)
{
	int ret = 0;
	int i =0;
	struct arpc_client_ctx *client_ctx = NULL;
	struct arpc_session_handle *session = NULL;
	uint32_t idle_thread_num;
	struct arpc_connection *con;
	struct xio_connection_params xio_con_param;
	struct arpc_connection_param conn_param;
	struct arpc_proto_new_session req_new;
	struct tp_param pool_param;
	uint32_t rx_con_num = 1;
	struct tp_thread_work thread;
	//LOG_THEN_RETURN_VAL_IF_TRUE(!param->ops, NULL, "ops is null.");

	session = arpc_create_session(ARPC_SESSION_CLIENT, sizeof(struct arpc_client_ctx));
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

	session->msg_iov_max_len  = (param->opt.msg_iov_max_len && param->opt.msg_iov_max_len <= (DATA_DEFAULT_MAX_LEN))?
								param->opt.msg_iov_max_len:
								get_option()->msg_iov_max_len;
	session->msg_data_max_len = (param->opt.msg_data_max_len && 
								param->opt.msg_data_max_len <= (4*1024*1024))?
								param->opt.msg_data_max_len:
								get_option()->msg_data_max_len;
	session->msg_head_max_len = (param->opt.msg_head_max_len && param->opt.msg_head_max_len <= (2048))?
								param->opt.msg_head_max_len:
								get_option()->msg_head_max_len;

	req_new.max_head_len = session->msg_head_max_len;
	req_new.max_data_len = session->msg_data_max_len;
	req_new.max_iov_len  = session->msg_iov_max_len;
	ARPC_LOG_NOTICE("req_new.max_data_len:%lu.", req_new.max_data_len);
	if (param->req_data && param->req_data_len && param->req_data_len < MAX_SESSION_REQ_DATA_LEN) {
		client_ctx->xio_param.private_data = arpc_mem_alloc(sizeof(struct arpc_tlv) + sizeof(struct arpc_proto_new_session), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!client_ctx->xio_param.private_data, error_1, "arpc_mem_alloc fail.");
		arpc_write_tlv(ARPC_PROTO_NEW_SESSION, sizeof(struct arpc_proto_new_session), client_ctx->xio_param.private_data);
		pack_new_session(&req_new, 
						(uint8_t*)client_ctx->xio_param.private_data + sizeof(struct arpc_tlv), 
						sizeof(struct arpc_proto_new_session));
		client_ctx->xio_param.private_data_len = sizeof(struct arpc_tlv) + sizeof(struct arpc_proto_new_session);
		ARPC_LOG_NOTICE("private_data_le:%u.", client_ctx->xio_param.private_data_len);
	}else{
		client_ctx->xio_param.private_data = arpc_mem_alloc(sizeof(struct arpc_tlv) + sizeof(struct arpc_proto_new_session), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!client_ctx->xio_param.private_data, error_1, "arpc_mem_alloc fail.");
		arpc_write_tlv(ARPC_PROTO_NEW_SESSION, sizeof(struct arpc_proto_new_session), client_ctx->xio_param.private_data);
		pack_new_session(&req_new,
						 (uint8_t*)client_ctx->xio_param.private_data + sizeof(struct arpc_tlv), 
						sizeof(struct arpc_proto_new_session));
		client_ctx->xio_param.private_data_len = sizeof(struct arpc_tlv) + sizeof(struct arpc_proto_new_session);
		ARPC_LOG_NOTICE("private_data_le:%u.", client_ctx->xio_param.private_data_len);
	}

	pool_param.cpu_max_num = 16;
	pool_param.thread_max_num = (param->con_num > 0)?(param->con_num*2 + 2 + 1):12;	// 默认是1:3的关系	,一个状态线程				
	session->threadpool = tp_create_thread_pool(&pool_param);

	idle_thread_num = (param->con_num > 0)? param->con_num : 2; // 默认是两个链接

	memset(&conn_param, 0, sizeof(struct arpc_connection_param));
	conn_param.type = ARPC_CON_TYPE_CLIENT;
	conn_param.session = session;
	conn_param.timeout_ms = (param->timeout_ms > ARPC_CLIENT_LOOP_MIN_TIME_MS)?(param->timeout_ms):(-1);
	if (conn_param.timeout_ms > 0){
		SET_FLAG(session->flags, ARPC_SESSION_ATTR_AUTO_DISCONNECT);
	}
	rx_con_num = (param->rx_con_num > 0 && param->rx_con_num <= (idle_thread_num/2))?(param->rx_con_num):(idle_thread_num/2);

	for (i = 0; i < idle_thread_num; i++) {
		conn_param.id = i + ARPC_CONN_ID_OFFSET;
		if (i < rx_con_num) {
			conn_param.io_type = ARPC_IO_TYPE_IN;
		}else{
			conn_param.io_type = ARPC_IO_TYPE_IO;
		}
		con = arpc_create_connection(&conn_param);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!con, error_2, "arpc_create_connection fail.");
		ret = session_insert_con(session, con);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!con, destroy_conn, "session_insert_con fail.");
	}

	if (!IS_SET(param->flags, ARPC_SESSION_ARRT_CLIENT_CONNECT_ON_USE)) {
		ret = arpc_session_connect_for_client(session, 3*1000);//等待至少一条链路可用
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, error_2, "client session connect server[%s] fail", client_ctx->uri);
	}
	ARPC_LOG_NOTICE("ARPC version[%s].", arpc_version());
	ARPC_LOG_NOTICE("Create session[%p] success, work thread num[%u], rx num[%u]!!", session, idle_thread_num, rx_con_num);

	thread.loop = &arpc_client_deamon;
	thread.stop = NULL;
	thread.usr_ctx = (void*)session;

	tp_post_one_work(session->threadpool, &thread, WORK_DONE_AUTO_FREE);

	return (arpc_session_handle_t)session;

destroy_conn:
	if(con) {
		arpc_destroy_connection(con);
	}
error_2:
	SAFE_FREE_MEM(client_ctx->private_data);
error_1:
	arpc_destroy_session(session, CLIENT_DESTROY_SESSION_MAX_TIME);
	ARPC_LOG_ERROR( "create session fail, exit.");
	return NULL;
}

int arpc_client_destroy_session(arpc_session_handle_t *fd)
{
	int ret;
	struct arpc_client_ctx *client_ctx = NULL;
	struct arpc_session_handle *session = NULL;
	void *thread_pool;
	LOG_THEN_RETURN_VAL_IF_TRUE(!fd, -1, "client session handle is null fail.");

	session = (struct arpc_session_handle *)(*fd);
	client_ctx = (struct arpc_client_ctx *)session->ex_ctx;
	SAFE_FREE_MEM(client_ctx->private_data);
	thread_pool = session->threadpool;
	ret = arpc_destroy_session(session, CLIENT_DESTROY_SESSION_MAX_TIME);//todo
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_destroy_session[%p] fail.", session);
	ARPC_LOG_NOTICE( "destroy session[%p] success.", session);
	tp_destroy_thread_pool(&thread_pool);
	*fd = NULL;
	return ret;
}

static int client_session_established(struct xio_session *session, struct xio_new_session_rsp *rsp, void *session_context)
{
	SESSION_CTX(session_ctx, session_context);
	ARPC_LOG_TRACE("session established for client");
	return session_established_for_client(session_ctx);
}

static int client_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *session_context)
{
	int ret = 0;
	struct arpc_connection *con_ctx = NULL;
	struct xio_connection_params xio_con_param;
	SESSION_CTX(session_ctx, session_context);

	con_ctx = (struct arpc_connection *)event_data->conn_user_context;
	ARPC_LOG_TRACE("#### event:%d|%s. reason: %s.", event_data->event,
					xio_session_event_str(event_data->event), 
					xio_strerror(event_data->reason));
	
	switch (event_data->event) {
		case XIO_SESSION_TEARDOWN_EVENT:
			xio_session_destroy(session);
			ret = session_client_teardown_event(session_ctx);
			if(ret) {
				ARPC_LOG_ERROR("session_rebuild_for_client fail.");
			}
			break;
		case XIO_SESSION_CONNECTION_ESTABLISHED_EVENT:
			ret = arpc_set_connect_status(con_ctx);
			if(ret) {
				ARPC_LOG_ERROR("arpc_set_connect_status error");
			}
			ret = session_notify_wakeup(session_ctx);
			LOG_ERROR_IF_VAL_TRUE(ret, "session_notify_wakeup fail.");
			break;
		case XIO_SESSION_CONNECTION_TEARDOWN_EVENT: // conn断开，需要释放con资源
			ret = arpc_set_disconnect_status(con_ctx);
			if (ret) {
				ARPC_LOG_ERROR("arpc_set_disconnect_status error");
			}
			xio_connection_destroy(event_data->conn);
			break;
		case XIO_SESSION_REJECT_EVENT:
			break;
		case XIO_SESSION_CONNECTION_REFUSED_EVENT: /**< connection refused event*/
			ARPC_LOG_ERROR(" build connection[%u] refused!.", con_ctx->id);
			break;
		case XIO_SESSION_ERROR_EVENT:
			break;
		default:
			break;
	};
	
	return ret;
}