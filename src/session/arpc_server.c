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
#include <sys/prctl.h>

#include "arpc_com.h"
#include "arpc_session.h"
#include "arpc_xio_callback.h"

#ifdef 	_DEF_SESSION_SERVER

static struct xio_session_ops x_server_ops = {
	.on_session_event			=  &server_session_event,
	.on_new_session				=  &server_on_new_session,
	.rev_msg_data_alloc_buf		=  &msg_head_process,
	.on_msg						=  &msg_data_process,
	.on_msg_delivered			=  &msg_delivered,
	.on_msg_send_complete		=  &response_send_complete,
	.on_ow_msg_send_complete	=  &oneway_send_complete,
	.on_msg_error				=  &message_error
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

	con_param = param->con;
	server->msg_iov_max_len  = (param->iov_max_len && param->iov_max_len <= (4*1024))?
								param->iov_max_len:
								(IOV_DEFAULT_MAX_LEN);
	server->msg_data_max_len = (param->opt.msg_data_max_len && 
								param->opt.msg_data_max_len <= (4*1024*1024))?
								param->opt.msg_data_max_len:
								(DATA_DEFAULT_MAX_LEN);
	server->msg_head_max_len = (param->opt.msg_head_max_len && param->opt.msg_head_max_len <= (1024))?
								param->opt.msg_head_max_len:
								(MAX_HEADER_DATA_LEN);

	ret = get_uri(&con_param, server->uri, URI_MAX_LEN);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, error_1, "arpc_create_server fail");
	work_num = tp_get_pool_idle_num(server->threadpool);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(work_num < ARPC_MIN_THREAD_IDLE_NUM, error_1, "idle thread num is low then min [%u]", ARPC_MIN_THREAD_IDLE_NUM);
	work_num = work_num - ARPC_MIN_THREAD_IDLE_NUM;
	work_num = (param->work_num && param->work_num <= work_num)? param->work_num : 2; // 默认只有1个主线程,2个工作线程
	for (i = 0; i < work_num; i++) {
		con_param.ipv4.port++; // 端口递增
		work_handle = arpc_create_xio_server_work(&con_param, server, &x_server_ops, i);
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
	prctl(PR_SET_NAME, "arpc_master");

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
