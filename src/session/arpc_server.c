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
#include "arpc_server.h"
#include "arpc_proto.h"

#ifdef 	_DEF_SESSION_SERVER

#define WAIT_THREAD_RUNING_TIMEOUT (1000)

struct arpc_new_session_ctx{
	void *new_session_usr_ctx;
	struct arpc_server_handle *server;
	void *server_usr_ctx;
};

static char *alloc_new_work_uri(struct arpc_server_work* work, const char *req_uri, uint16_t uri_len);

static int server_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *session_context);
static int server_on_new_session(struct xio_session *session,struct xio_new_session_req *req, void *server_context);

static struct arpc_server_work *arpc_create_work();
static int arpc_destroy_work(struct arpc_server_work* work);

static void destroy_work_handle(QUEUE* work_q);
static void destroy_session_handle(QUEUE* session_q);
static int xio_server_work_run(void * ctx);
static void xio_server_work_stop(void * ctx);

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
	struct arpc_server_work *work_handle;
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
	server->msg_iov_max_len  = (param->iov_max_len && param->iov_max_len <= (DATA_DEFAULT_MAX_LEN))?
								param->iov_max_len:
								get_option()->msg_iov_max_len;
	server->msg_data_max_len = (param->opt.msg_data_max_len && 
								param->opt.msg_data_max_len <= (4*1024*1024))?
								param->opt.msg_data_max_len:
								get_option()->msg_data_max_len;
	server->msg_head_max_len = (param->opt.msg_head_max_len && param->opt.msg_head_max_len <= (2048))?
								param->opt.msg_head_max_len:
								get_option()->msg_head_max_len;

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
	if (server->server)
		xio_unbind(server->server);
	server->server = NULL;

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
static int server_session_event(struct xio_session *session, struct xio_session_event_data *event_data, void *session_context)
{
	struct xio_connection_attr attr;
	int ret;
	struct arpc_connection *con;
	struct arpc_server_work *work;
	struct arpc_new_session_ctx *session_ctx;
	struct arpc_connection_param param;
	SESSION_CTX(session_fd, session_context);

	ARPC_LOG_TRACE("##### session event:%d ,%s. reason: %s.",event_data->event,
					xio_session_event_str(event_data->event),
	       			xio_strerror(event_data->reason));
	session_ctx = (struct arpc_new_session_ctx *)session_fd->ex_ctx;
	switch (event_data->event) {
		case XIO_SESSION_NEW_CONNECTION_EVENT:	//新的链路
			if(event_data->conn_user_context == session_ctx->server){
				ARPC_LOG_TRACE("##### new connection[%p] is main thread,not need build.", event_data->conn);
				break;
			}
			work = (struct arpc_server_work *)event_data->conn_user_context;
			memset(&param, 0, sizeof(struct arpc_connection_param));
			param.id = work->affinity;
			param.session = session_fd;
			param.xio_con_ctx = work->work_ctx;
			param.usr_ctx = session_fd->usr_context;
			param.type = ARPC_CON_TYPE_SERVER;
			param.xio_con = event_data->conn;
			param.timeout_ms = XIO_INFINITE;
			con = arpc_create_connection(&param);
			LOG_THEN_RETURN_VAL_IF_TRUE(!con, -1, "arpc_create_connection fail.");

			memset(&attr, 0, sizeof(struct xio_connection_attr));
			attr.user_context = con;
			ARPC_LOG_TRACE("##### new connection[%u][%p] create success.", con->id, con);
			xio_modify_connection(event_data->conn, &attr, XIO_CONNECTION_ATTR_USER_CTX);

			ret = session_insert_con(session_fd, con);
			LOG_ERROR_IF_VAL_TRUE(ret, "session_remove_con fail.");
			
			ret = arpc_set_connect_status(con);
			if(ret) {
				ARPC_LOG_ERROR("arpc_set_connect_status error");
			}

			break;
		case XIO_SESSION_CONNECTION_TEARDOWN_EVENT:
			if(event_data->conn_user_context != session_ctx->server){
				con = (struct arpc_connection *)event_data->conn_user_context;
				ARPC_LOG_TRACE("##### connection[%u][%p] teardown.", con->id, con);
				ret = session_remove_con(session_fd, con);
				LOG_ERROR_IF_VAL_TRUE(ret, "session_remove_con fail.");
				arpc_set_disconnect_status(con);
				arpc_destroy_connection(con);
			}else{
				ARPC_LOG_TRACE("connection[%p] is main thread, need tear down.", event_data->conn);
			}
			xio_connection_destroy(event_data->conn);
			break;
		case XIO_SESSION_TEARDOWN_EVENT:
			ARPC_LOG_TRACE("##### session[%p] teardown.", session_fd);
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
			xio_session_destroy(session);
			break;
		case XIO_SESSION_ERROR_EVENT:
			break;
		default:
			break;
	};

	return 0;
}

static int server_on_new_session(struct xio_session *session,struct xio_new_session_req *req, void *server_context)
{
	struct arpc_new_session_req client;
	struct arpc_con_info *ipv4;
	struct arpc_new_session_rsp param;
	struct arpc_session_handle *new_session = NULL;
	struct arpc_new_session_ctx *new_session_ctx = NULL;
	struct xio_session_attr attr = {0};
	int ret;
	char	**uri_vec;
	void *req_addr;
	uint64_t req_len = 0;
	arpc_proto_t tlv_type = 0;
	struct arpc_proto_new_session new_req = {0};
	uint32_t i;
	QUEUE* work_q;
	uint32_t work_num = 0;
	struct arpc_server_work *work_handle;
	char req_uri[256] = {0};
	struct arpc_server_handle *server_fd = (struct arpc_server_handle *)server_context;

	LOG_THEN_RETURN_VAL_IF_TRUE((!session || !req || !server_fd), -1, "invalid input.");

	memset(&client, 0, sizeof(struct arpc_new_session_req));
	memset(&param, 0, sizeof(param));

	param.rsp_data = NULL;
	param.rsp_data_len  = 0;
	param.ret_status = ARPC_E_STATUS_OK;

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!server_fd->new_session_start, reject, "new_session_start callback null.");
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!server_fd->new_session_end, reject, "new_session_end callback null.");

	if(req->uri && req->uri_len){
		memcpy(req_uri, req->uri, req->uri_len);
	}
	ARPC_LOG_NOTICE("session uri:%s", req_uri);

	ipv4 = &client.client_con_info;
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((req->proto != XIO_PROTO_TCP), reject, "no tcp con, fail.");
	ipv4->type =ARPC_E_TRANS_TCP;
	arpc_get_ipv4_addr(&req->src_addr, ipv4->ipv4.ip, IPV4_MAX_LEN, &ipv4->ipv4.port);

	if (req->private_data) {
		arpc_read_tlv(&tlv_type, &req_len, &req_addr, req->private_data);
		assert(tlv_type == ARPC_PROTO_NEW_SESSION);
		assert(req_len == sizeof(struct arpc_proto_new_session));
		unpack_new_session(req_addr, sizeof(struct arpc_proto_new_session), &new_req);
	}

	client.client_data.data = NULL;//todo
	client.client_data.len = 0;

	ret = server_fd->new_session_start(&client, &param, server_fd->usr_context);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ret != ARPC_SUCCESS), reject, "new_session_start return fail.");

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((param.ret_status != ARPC_E_STATUS_OK), reject, "ret_status[%d] not ok.", param.ret_status);

	// 尝试新建session
	new_session = arpc_create_session(ARPC_SESSION_SERVER, sizeof(struct arpc_new_session_ctx));
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
	new_session->msg_data_max_len = (new_req.max_data_len)?new_req.max_data_len:server_fd->msg_data_max_len;
	new_session->msg_head_max_len = (new_req.max_head_len)?new_req.max_head_len:server_fd->msg_head_max_len;
	new_session->msg_iov_max_len = (new_req.max_iov_len)?new_req.max_iov_len:server_fd->msg_iov_max_len;
	attr.ses_ops = NULL;
	attr.uri = NULL;
	attr.user_context = (void*)new_session;

	ret = xio_modify_session(session, &attr, XIO_SESSION_ATTR_USER_CTX);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ret != ARPC_SUCCESS), reject, "xio_modify_session fail.");
	new_session->status = ARPC_SES_STA_ACTIVE;
	if(server_fd->work_num) {
		uri_vec = arpc_mem_alloc(server_fd->work_num * sizeof(char *), NULL);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!uri_vec, reject, "arpc_mem_alloc fail.");
		work_num = 0;
		QUEUE_FOREACH_VAL(&server_fd->q_work, work_q, 
		{
			if (work_num < server_fd->work_num) { 
				work_handle = QUEUE_DATA(work_q, struct arpc_server_work, q);
				uri_vec[work_num] = alloc_new_work_uri(work_handle, req_uri, strlen(req_uri));
				if (uri_vec[work_num]) {
				   ARPC_LOG_NOTICE("work uri:%s.", uri_vec[work_num]);
				   work_num++;
				}
				continue;
			}
			break;
		});
		new_session->conn_num = work_num;
		xio_accept(session, (const char **)uri_vec, work_num, param.rsp_data, param.rsp_data_len); 
		for (i = 0; i < work_num; i++) {
			arpc_mem_free(uri_vec[i], NULL);
		}
		arpc_mem_free(uri_vec, NULL);
		uri_vec = NULL;
	}else{
		xio_accept(session, NULL, 0, param.rsp_data, param.rsp_data_len); 
	}

	ret = server_insert_session(server_fd, new_session);
	LOG_ERROR_IF_VAL_TRUE(ret, "server_insert_session fail.");
	server_fd->new_session_end((arpc_session_handle_t)new_session, &param, server_fd->usr_context);
	
	ARPC_LOG_NOTICE("create new session[%p] success, client[%s:%u].", new_session, ipv4->ipv4.ip, ipv4->ipv4.port);
	return 0;
reject:
	if(new_session)
		arpc_destroy_session(new_session, 0);
	new_session = NULL;
	xio_reject(session, XIO_E_SESSION_ABORTED, param.rsp_data, param.rsp_data_len); // 拒绝session请求
	server_fd->new_session_end(NULL, &param, server_fd->usr_context);
	return -1;
}

// server
struct arpc_server_handle *arpc_create_server(uint32_t ex_ctx_size)
{
	int ret = 0;
	int i =0;
	struct arpc_server_handle *svr = NULL;
	/* handle*/
	svr = (struct arpc_server_handle *)arpc_mem_alloc(sizeof(struct arpc_server_handle) + ex_ctx_size, NULL);
	if (!svr) {
		ARPC_LOG_ERROR( "malloc error, exit ");
		return NULL;
	}
	memset(svr, 0, sizeof(struct arpc_server_handle) + ex_ctx_size);
	ret = arpc_mutex_init(&svr->lock); /* 初始化互斥锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, NULL, "arpc_mutex_init fail.");
	QUEUE_INIT(&svr->q_session);
	QUEUE_INIT(&svr->q_work);
	svr->iov_max_len = IOV_DEFAULT_MAX_LEN;
	svr->threadpool = arpc_get_threadpool();
	return svr;
}

int arpc_destroy_server(struct arpc_server_handle* svr)
{
	int ret = 0;
	QUEUE* q;

	LOG_THEN_RETURN_VAL_IF_TRUE(!svr, -1, "svr null.");

	ret = arpc_mutex_lock(&svr->lock); /* 锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_mutex_lock fail.");

	q = NULL;
	QUEUE_FOREACH_VAL(&svr->q_session, q, destroy_session_handle(q));
	q = NULL;
	QUEUE_FOREACH_VAL(&svr->q_work, q, destroy_work_handle(q));

	ret = arpc_mutex_unlock(&svr->lock); /* 开锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_mutex_unlock fail.");

	ret = arpc_mutex_destroy(&svr->lock); /* 初始化互斥锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_mutex_destroy fail.");
	
	SAFE_FREE_MEM(svr);
	return 0;
}

struct arpc_server_work *arpc_create_xio_server_work(const struct arpc_con_info *con_param, 
													struct arpc_server_handle* server, 
													struct xio_session_ops *work_ops,
													uint32_t index)
{
	struct arpc_server_work *work;
	int ret;
	work_handle_t hd;
	struct tp_thread_work thread;
	struct xio_context_params ctx_params;

	work = arpc_create_work();
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, NULL, "arpc_create_work fail.");
	work->affinity = index + 1;

	(void)memset(&ctx_params, 0, sizeof(struct xio_context_params));
	ctx_params.max_inline_xio_data = server->msg_data_max_len;
	ctx_params.max_inline_xio_hdr = server->msg_head_max_len;

	work->work_ctx = xio_context_create(&ctx_params, 0, work->affinity);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!work->work_ctx, free_work, "xio_context_create fail.");

	ret = get_uri(con_param, work->uri, URI_MAX_LEN);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_work, "get_uri fail");

	ARPC_LOG_NOTICE("set uri[%s] on work[%u].", work->uri, index);

	work->work = xio_bind(work->work_ctx, work_ops, work->uri, NULL, 0, work);

	ret = arpc_cond_lock(&work->cond);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_xio_work, "arpc_cond_lock fail.");
	thread.loop = &xio_server_work_run;
	thread.stop = &xio_server_work_stop;
	thread.usr_ctx = (void*)work;
	work->thread_handle = tp_post_one_work(server->threadpool, &thread, WORK_DONE_AUTO_FREE);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!work->thread_handle, free_cond, "tp_post_one_work fail.");

	ret = arpc_cond_wait_timeout(&work->cond, WAIT_THREAD_RUNING_TIMEOUT);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, cancel_work, "wait thread runing fail.");

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(work->status != ARPC_WORK_STA_RUN, cancel_work, "work run status[%d] fail.", work->status);
	arpc_cond_unlock(&work->cond);

	ARPC_LOG_NOTICE("create work server uri[%s] success.", work->uri);
	return work;

cancel_work:
	tp_cancel_one_work(work->thread_handle);
free_cond:
	arpc_cond_unlock(&work->cond);
free_xio_work:
	xio_context_destroy(work->work_ctx);
free_work:
	arpc_destroy_work(work);
	return NULL;
}

int  arpc_destroy_xio_server_work(struct arpc_server_work *work)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, -1, "arpc_server_work is null fail.");

	arpc_cond_lock(&work->cond);

	ret = tp_cancel_one_work(work->thread_handle);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "tp_cancel_one_work fail.");
	arpc_cond_wait_timeout(&work->cond, WAIT_THREAD_RUNING_TIMEOUT);
	
	arpc_cond_unlock(&work->cond);

	xio_context_destroy(work->work_ctx);
	arpc_destroy_work(work);
	return 0;
unlock:
	arpc_cond_unlock(&work->cond);
	return -1;
}

int server_insert_work(struct arpc_server_handle *server, struct arpc_server_work *work)
{
	int ret;
	ret = arpc_mutex_lock(&server->lock);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_mutex_lock server[%p] fail.", server);
	QUEUE_INSERT_TAIL(&server->q_work, &work->q);
	server->work_num++;
	arpc_mutex_unlock(&server->lock);
	return 0;
}

int server_remove_work(struct arpc_server_handle *server, struct arpc_server_work *work)
{
	int ret;
	ret = arpc_mutex_lock(&server->lock);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_mutex_lock server[%p] fail.", server);
	QUEUE_REMOVE(&work->q);
	QUEUE_INIT(&work->q);
	server->work_num--;
	arpc_mutex_unlock(&server->lock);
	return 0;
}

int server_insert_session(struct arpc_server_handle *server, struct arpc_session_handle *session)
{
	int ret;
	ret = arpc_mutex_lock(&server->lock);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_mutex_lock server[%p] fail.", server);
	QUEUE_INSERT_TAIL(&server->q_work, &session->q);
	arpc_mutex_unlock(&server->lock);
	return 0;
}

int server_remove_session(struct arpc_server_handle *server, struct arpc_session_handle *session)
{
	int ret;
	ret = arpc_mutex_lock(&server->lock);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_mutex_lock server[%p] fail.", server);
	QUEUE_REMOVE(&session->q);
	QUEUE_INIT(&session->q);
	arpc_mutex_unlock(&server->lock);
	return 0;
}

//server inter
static struct arpc_server_work *arpc_create_work()
{
	int ret = 0;
	struct arpc_server_work *work = NULL;
	
	/* handle*/
	work = (struct arpc_server_work *)arpc_mem_alloc(sizeof(struct arpc_server_work), NULL);
	if (!work) {
		ARPC_LOG_ERROR( "malloc error, exit ");
		return NULL;
	}
	memset(work, 0, sizeof(struct arpc_server_work));

	ret = arpc_cond_init(&work->cond); 
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, NULL, "arpc_cond_init fail.");
	QUEUE_INIT(&work->q);
	work->magic = ARPC_WROK_MAGIC;
	return work;
}

static int arpc_destroy_work(struct arpc_server_work* work)
{
	int ret = 0;
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, ARPC_ERROR, "work null.");
	ret = arpc_cond_destroy(&work->cond); 
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_destroy fail.");
	SAFE_FREE_MEM(work);
	return 0;
}

static void destroy_work_handle(QUEUE* work_q)
{
	struct arpc_server_work *work_handle;
	int ret;

	LOG_THEN_RETURN_VAL_IF_TRUE(!work_q, ;, "work_q null.");
	work_handle = QUEUE_DATA(work_q, struct arpc_server_work, q);
	LOG_THEN_RETURN_VAL_IF_TRUE(!work_handle, ;, "work_handle null.");

	ret = arpc_destroy_xio_server_work(work_handle);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ;, "arpc_destroy_session fail.");
}

static void destroy_session_handle(QUEUE* session_q)
{
	struct arpc_session_handle *session;
	int ret;

	LOG_THEN_RETURN_VAL_IF_TRUE(!session_q, ;, "session_q null.");
	session = QUEUE_DATA(session_q, struct arpc_session_handle, q);
	LOG_THEN_RETURN_VAL_IF_TRUE(!session, ;, "session null.");

	ret = arpc_destroy_session(session, 0);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ;, "arpc_destroy_session fail.");
}

static int xio_server_work_run(void * ctx)
{
	struct arpc_server_work *work = (struct arpc_server_work *)ctx;
	//cpu_set_t		cpuset;
	char thread_name[16+1] ={0};
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, ARPC_ERROR, "work null fail.");
	ARPC_LOG_DEBUG("work run on the thread[%lu].", pthread_self());
	arpc_cond_lock(&work->cond);
	work->status = ARPC_WORK_STA_RUN;

	snprintf(thread_name, sizeof(thread_name), "arpc_work_%d", work->affinity);
	prctl(PR_SET_NAME, thread_name);

	/*CPU_ZERO(&cpuset);
	CPU_SET((work->affinity)%(arpc_cpu_max_num()), &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);*/

	arpc_cond_notify(&work->cond);
	arpc_cond_unlock(&work->cond);
	for(;;){	
		if (xio_context_run_loop(work->work_ctx, XIO_INFINITE) < 0)
			ARPC_LOG_ERROR("xio error msg: %s.", xio_strerror(xio_errno()));
		ARPC_LOG_DEBUG("xio context run loop pause...");
		arpc_cond_lock(&work->cond);
		if(work->status == ARPC_WORK_STA_EXIT){
			arpc_cond_notify(&work->cond);
			break;
		}
		arpc_cond_unlock(&work->cond);
		arpc_sleep(1);
	}
	ARPC_LOG_NOTICE("xio server work[%p] on thread[%lu] exit now.", work, pthread_self());
	arpc_cond_unlock(&work->cond);
	return ARPC_SUCCESS;
}

static void xio_server_work_stop(void * ctx)
{
	struct arpc_server_work *work = (struct arpc_server_work *)ctx;
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, ;, "work null fail.");
	work->status = ARPC_WORK_STA_EXIT;
	if(work->work_ctx)
		xio_context_stop_loop(work->work_ctx);
	return;
}

static char *alloc_new_work_uri(struct arpc_server_work* work, const char *req_uri, uint16_t uri_len)
{
	char proto[16] = {0};
	char port[16] = {0};
	char addr[32]= {0};
	int ret;
	char *out;

	LOG_THEN_RETURN_VAL_IF_TRUE(!work, NULL, "work null fail.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_uri, NULL, "work null fail.");

	ret = arpc_uri_get_proto(req_uri, proto, 16);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, NULL, "arpc_uri_get_proto fail.");

	ret = arpc_uri_get_resource(req_uri, addr, 32);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, NULL, "arpc_uri_get_resource fail.");

	ret = arpc_uri_get_portal(work->uri, port, 16);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, NULL, "arpc_uri_get_portal fail.");

	out = arpc_mem_alloc(256, NULL);
	LOG_THEN_RETURN_VAL_IF_TRUE(!out, NULL, "arpc_mem_alloc fail.");

	memset((void*)out, 0, 256);

	ret = sprintf(out, "%s://%s:%s", proto, addr, port);//这里需要获取类型
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret < 0, free_buf, "sprintf fail.");

	return out;
free_buf:
	SAFE_FREE_MEM(out);
	return NULL;
}
#endif
