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
#include "arpc_request.h"
#include "arpc_response.h"
#include "arpc_xio_callback.h"

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
	session->msg_iov_max_len  = (param->opt.msg_iov_max_len && param->opt.msg_iov_max_len <= (4*1024))?
								param->opt.msg_iov_max_len:
								(IOV_DEFAULT_MAX_LEN);
	session->msg_data_max_len = (param->opt.msg_data_max_len && 
								param->opt.msg_data_max_len <= (4*1024*1024))?
								param->opt.msg_data_max_len:
								(DATA_DEFAULT_MAX_LEN);
	session->msg_head_max_len = (param->opt.msg_head_max_len && param->opt.msg_head_max_len <= (1024))?
								param->opt.msg_head_max_len:
								(MAX_HEADER_DATA_LEN);
	ARPC_LOG_NOTICE("session->msg_iov_max_len:%u", session->msg_iov_max_len);
	ARPC_LOG_NOTICE("session->msg_data_max_len:%lu", session->msg_data_max_len);
	ARPC_LOG_NOTICE("session->msg_head_max_len:%u", session->msg_head_max_len);

	idle_thread_num = tp_get_pool_idle_num(session->threadpool);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(idle_thread_num < ARPC_MIN_THREAD_IDLE_NUM, error_2, "idle_thread_num[%u] is low 2.", idle_thread_num);

	ARPC_LOG_NOTICE("Max idle thread num[%u], user expact max num[%u], rx num[%u].", idle_thread_num, param->con_num, param->rx_con_num);
	idle_thread_num = idle_thread_num - ARPC_MIN_THREAD_IDLE_NUM;
	idle_thread_num = (param->con_num && param->con_num < idle_thread_num)? param->con_num : idle_thread_num; // 默认是两个链接

	idle_thread_num = (idle_thread_num < ARPC_CLIENT_MAX_CON_NUM)? idle_thread_num: ARPC_CLIENT_MAX_CON_NUM;

	rx_con_num = (param->rx_con_num > 0)? param->rx_con_num: 0; //至少保留一个接收通道
	rx_con_num = (rx_con_num <= (idle_thread_num/2))? rx_con_num: (idle_thread_num/2); //接收通道最多是总链路的一半
	for (i = 0; i < idle_thread_num; i++) {
		con = arpc_create_connection(ARPC_CON_TYPE_CLIENT, session, i);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!con, error_2, "arpc_init_client_conn fail.");
		if(rx_con_num){
			rx_con_num--;
			set_connection_mode(con, ARPC_CON_MODE_DIRE_IN);
			ARPC_LOG_NOTICE("connection[%u][%p] set rx mode!!", i, con);
		}
	}

	ret = session_get_conn(session, &con, 5*1000);//等待至少一条链路可用
	LOG_ERROR_IF_VAL_TRUE(ret, "wait_connection_finished timeout....");


	ARPC_LOG_NOTICE("Create session[%p] success, work thread num[%u]!!", session, idle_thread_num);

	return (arpc_session_handle_t)session;

error_2:
	SAFE_FREE_MEM(client_ctx->private_data);
error_1:
	arpc_destroy_session(session, 60*1000);
	ARPC_LOG_ERROR( "create session fail, exit.");
	return NULL;
}

int arpc_client_destroy_session(arpc_session_handle_t *fd)
{
	int ret;
	struct arpc_session_handle *session = NULL;
	LOG_THEN_RETURN_VAL_IF_TRUE(!fd, -1, "client session handle is null fail.");

	session = (struct arpc_session_handle *)(*fd);
	ret = arpc_destroy_session(session, 60*1000);//todo
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_destroy_session[%p] fail.", session);
	ARPC_LOG_NOTICE( "destroy session[%p] success.", session);
	*fd = NULL;
	return ret;
}

enum arpc_session_status arpc_get_session_status(const arpc_session_handle_t fd)
{
	return ARPC_SESSION_STA_ACTIVE;
}

