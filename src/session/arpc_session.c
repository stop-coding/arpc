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
#include <unistd.h>
#include <arpa/inet.h>
#include <sched.h>
#include<fcntl.h>
#include <sys/prctl.h>

#include "arpc_session.h"

#define WAIT_THREAD_RUNING_TIMEOUT (1000)

static const char pipe_magic[] = "msg";

#define ARPC_CONN_TX_MAX_DEPTH	  		50
#define MAX_TX_CNT_PER_WAKEUP			10

// client
static struct arpc_connection *arpc_new_connection();
static int arpc_delete_connection(struct arpc_connection* con);
static int xio_client_run_con(void * ctx);
static void xio_client_stop_con(struct arpc_connection* con);
static void destroy_connection_handle(QUEUE* con_q);
static void dis_connection(QUEUE* con_q);
static int arpc_init_client_conn(struct arpc_connection *con, struct arpc_session_handle *s, uint32_t index);
static int  arpc_finish_client_conn(struct arpc_connection *con);
static struct arpc_connection *arpc_create_xio_server_con(struct arpc_session_handle *s);
static int  arpc_destroy_xio_server_con(struct arpc_connection *con);

//server
static struct arpc_work_handle *arpc_create_work();
static void destroy_work_handle(QUEUE* work_q);
static void destroy_session_handle(QUEUE* session_q);
static int arpc_destroy_work(struct arpc_work_handle* work);
static int xio_server_work_run(void * ctx);
static void xio_server_work_stop(void * ctx);

struct arpc_session_handle *arpc_create_session(enum session_type type, uint32_t ex_ctx_size)
{
	int ret = 0;
	int i =0;
	struct arpc_session_handle *session = NULL;
	/* handle*/
	session = (struct arpc_session_handle *)ARPC_MEM_ALLOC(sizeof(struct arpc_session_handle) + ex_ctx_size, NULL);
	if (!session) {
		ARPC_LOG_ERROR( "malloc error, exit ");
		return NULL;
	}
	memset(session, 0, sizeof(struct arpc_session_handle) + ex_ctx_size);

	ret = arpc_cond_init(&session->cond); /* 初始化信号锁 */
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_buf, "arpc_cond_init fail.");

	QUEUE_INIT(&session->q);
	QUEUE_INIT(&session->q_con);
	session->threadpool = arpc_get_threadpool();
	session->type = type;
	session->is_close = 0;
	return session;

free_buf:
	SAFE_FREE_MEM(session);
	return NULL;
}

int arpc_destroy_session(struct arpc_session_handle* session, int64_t timeout_ms)
{
	int ret = 0;
	QUEUE* iter;
	struct arpc_connection *con;

	LOG_THEN_RETURN_VAL_IF_TRUE(!session, -1, "session null.");
	ret = arpc_cond_lock(&session->cond); /* 锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session fail.");
	session->is_close++;
	arpc_cond_notify_all(&session->cond);//通知释放资源
	arpc_cond_unlock(&session->cond);

	ret = arpc_cond_lock(&session->cond); /* 锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session fail.");

	// 断开链路
	QUEUE_FOREACH_VAL(&session->q_con, iter, 
	{
		con = QUEUE_DATA(iter, struct arpc_connection, q);
		ret = arpc_disconnection(con, timeout_ms);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_disconnection fail.");
	});

	if(timeout_ms && session->is_close <= 1){
		ret = arpc_cond_wait_timeout(&session->cond, timeout_ms);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_wait_timeout session[%p] free timeout.", session);
	}

	// 释放con资源
	while(!QUEUE_EMPTY(&session->q_con)){
		iter = QUEUE_HEAD(&session->q_con);
		QUEUE_REMOVE(iter);
		QUEUE_INIT(iter);
		con = QUEUE_DATA(iter, struct arpc_connection, q);
		ret = arpc_destroy_connection(con, timeout_ms);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_connection fail.");
	}

	arpc_cond_unlock(&session->cond);

	ret = arpc_cond_destroy(&session->cond);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_destroy fail.");

	SAFE_FREE_MEM(session);
	return 0;
}

int arpc_wait_session(struct arpc_session_handle *session, int64_t timeout_ms)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!session, ARPC_ERROR, "pcon is null.");
	ret = arpc_cond_lock(&session->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_mutex_lock session[%p] fail.", session);

	if(!session->is_close){
		arpc_cond_unlock(&session->cond);
		return 0;
	}
	ret = arpc_cond_wait_timeout(&session->cond, timeout_ms);
	if(ret || session->is_close){
		ARPC_LOG_ERROR("session[%p] connection fail, status[%u].", session, session->is_close);
		arpc_cond_unlock(&session->cond);
		return ARPC_ERROR;
	}
	arpc_cond_unlock(&session->cond);
	return 0;
}

int session_rebuild_for_client(struct arpc_session_handle *session)
{
	struct arpc_client_ctx *client_ctx;
	int ret;
	QUEUE* iter;
	struct xio_connection_params xio_con_param;
	struct arpc_connection *con = NULL;
	LOG_THEN_RETURN_VAL_IF_TRUE(!session, ARPC_ERROR, "session is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(session->type != SESSION_CLIENT, ARPC_ERROR, "session not client.");

	ret = arpc_cond_lock(&session->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail.");
	client_ctx = (struct arpc_client_ctx *)session->ex_ctx;
	session->xio_s = xio_session_create(&client_ctx->xio_param);
	LOG_THEN_RETURN_VAL_IF_TRUE(!session->xio_s, ARPC_ERROR, "xio_session_create fail.");
	QUEUE_FOREACH_VAL(&session->q_con, iter,
	{
		con = QUEUE_DATA(iter, struct arpc_connection, q);
		ret = arpc_cond_lock(&con->cond);
		if(ret){
			ARPC_LOG_ERROR("lock con[%p] fail.", con);
			continue;
		}
		(void)memset(&xio_con_param, 0, sizeof(struct xio_connection_params));
		xio_con_param.session			= session->xio_s;
		xio_con_param.ctx				= con->xio_con_ctx;
		xio_con_param.conn_idx			= con->client.affinity;
		xio_con_param.conn_user_context	= con;
		con->xio_con = xio_connect(&xio_con_param);
		LOG_ERROR_IF_VAL_TRUE(!con->xio_con, "xio_connect fail.");
		ret = arpc_cond_wait_timeout(&con->cond, 5*1000);
		if(!ret){
			ARPC_LOG_NOTICE("con[%u][%p] reconnection success.", con->id, con);
		}
		arpc_cond_unlock(&con->cond);
	});
	arpc_cond_unlock(&session->cond);
	ARPC_LOG_NOTICE("rebuild session[%p] success!!", session);

	return 0;
}

int session_insert_con(struct arpc_session_handle *s, struct arpc_connection *con)
{
	int ret;
	ret = arpc_cond_lock(&s->cond); /* 锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session[%p] fail.", s);
	QUEUE_INSERT_TAIL(&s->q_con, &con->q);
	s->conn_num++;
	arpc_cond_notify(&s->cond);
	arpc_cond_unlock(&s->cond);
	return 0;
}

int session_remove_con(struct arpc_session_handle *s, struct arpc_connection *con)
{
	int ret;
	ret = arpc_cond_lock(&s->cond); /* 锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session[%p] fail.", s);
	QUEUE_REMOVE(&con->q);
	QUEUE_INIT(&con->q);
	s->conn_num--;
	arpc_cond_notify(&s->cond);
	arpc_cond_unlock(&s->cond);
	return 0;
}

int session_get_conn(struct arpc_session_handle *s, struct arpc_connection **pcon, int64_t timeout_ms)
{
	int ret;
	QUEUE* iter;
	struct arpc_connection *con = NULL;

	LOG_THEN_RETURN_VAL_IF_TRUE(!pcon, ARPC_ERROR, "pcon is null.");
	ret = arpc_cond_lock(&s->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_mutex_lock session[%p] fail.", s);
	if(QUEUE_EMPTY(&s->q_con) || s->is_close){
		ARPC_LOG_ERROR("session[%p] invalid, close status[%u]", s, s->is_close);
		arpc_cond_unlock(&s->cond);
		return ARPC_ERROR;
	}
	while(!con){
		QUEUE_FOREACH_VAL(&s->q_con, iter,
		{
			con = QUEUE_DATA(iter, struct arpc_connection, q);
			ret = arpc_cond_trylock(&con->cond);
			if(ret){
				ARPC_LOG_NOTICE("connection[%u][%p] is runing.",con->id, con->xio_con);
				con = NULL;
				continue;
			}
			ARPC_LOG_DEBUG("thread[%lu],connection[%u][%p], status: tx_msg_num:%lu, sta:%d, dir:%d.",pthread_self(),
							con->id, con->xio_con, con->tx_msg_num, con->status, con->conn_mode);
			if((con->tx_msg_num < ARPC_CONN_TX_MAX_DEPTH) && 
				(con->status == XIO_STA_RUN_ACTION) && 
				(con->conn_mode == ARPC_CON_MODE_DIRE_OUT || 
				con->conn_mode == ARPC_CON_MODE_DIRE_IO)) {
				ARPC_LOG_DEBUG("connection[%u][%p] get well send.",con->id, con->xio_con);
				arpc_cond_unlock(&con->cond);
				break;
			}
			arpc_cond_unlock(&con->cond);
			con = NULL;
		});
		
		if(con){break;}
		ARPC_LOG_NOTICE("warning: session[%p][con_num:%u] no idle connection, wait[%ld ms]...", s, s->conn_num, timeout_ms);
		ret = arpc_cond_wait_timeout(&s->cond, timeout_ms);
		if (ret) {
			ARPC_LOG_ERROR("session[%p] wait idle connection timeout[%ld ms].", s, timeout_ms);
			break;
		}
	}
	*pcon = con;
	if (con) {
		s->tx_total++;
		QUEUE_REMOVE(&con->q);
		QUEUE_INSERT_TAIL(&s->q_con, &con->q);
	}
	arpc_cond_unlock(&s->cond);
	return (con)?0:-1;
}

// connection

int set_connection_mode(struct arpc_connection *con, enum arpc_connection_mode	conn_mode)
{
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "arpc_connection is null.");
	arpc_cond_lock(&con->cond);
	con->conn_mode = conn_mode; // 输入模式
	con->is_busy = 1;
	ARPC_LOG_DEBUG("set connection[%u][%p] mode[%d].", con->id, con, conn_mode);
	arpc_cond_unlock(&con->cond);
	return 0;
}

static struct arpc_connection *arpc_new_connection()
{
	int ret = 0;
	int flags;
	struct arpc_connection *con = NULL;
	/* handle*/
	con = (struct arpc_connection *)ARPC_MEM_ALLOC(sizeof(struct arpc_connection), NULL);
	if (!con) {
		ARPC_LOG_ERROR( "malloc error, exit ");
		return NULL;
	}
	memset(con, 0, sizeof(struct arpc_connection));
	ret = pipe(con->pipe_fd);
	ret = arpc_cond_init(&con->cond); 
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_buf, "arpc_mutex_init fail.");
	ret = pipe(con->pipe_fd);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_cond, "pipe init fail.");
	QUEUE_INIT(&con->q);
	QUEUE_INIT(&con->q_tx_msg);
	QUEUE_INIT(&con->q_tx_end);
	con->magic = ARPC_CONN_MAGIC;
	flags = fcntl(con->pipe_fd[0],F_GETFL);
	flags |= O_NONBLOCK;
	ret = fcntl(con->pipe_fd[0], F_SETFL, flags);
	
	return con;
free_cond:
	arpc_cond_destroy(&con->cond);
free_buf:
	SAFE_FREE_MEM(con);
	return NULL;
}

static int arpc_delete_connection(struct arpc_connection* con)
{
	int ret = 0;
	ret = arpc_cond_lock(&con->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock fail, maybe free already.");
	if(con->pipe_fd[0] > 0){
		close(con->pipe_fd[0]);
		con->pipe_fd[0] = -1;
	}
	if(con->pipe_fd[1] > 0){
		close(con->pipe_fd[1]);
		con->pipe_fd[1] = -1;
	}
	con->magic = 0;
	arpc_cond_unlock(&con->cond);
	ret = arpc_cond_destroy(&con->cond); 
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_destroy fail.");
	SAFE_FREE_MEM(con);
	return 0;
}

static int arpc_init_client_conn(struct arpc_connection *con, struct arpc_session_handle *s, uint32_t index)
{
	int ret;
	struct xio_connection_params xio_con_param;
	work_handle_t hd;
	struct tp_thread_work thread;
	struct xio_context_params ctx_params;

	LOG_THEN_RETURN_VAL_IF_TRUE(!con, -1, "arpc_new_connection fail.");
	con->client.affinity = index + 1;
	con->status = XIO_STA_INIT;
	(void)memset(&ctx_params, 0, sizeof(struct xio_context_params));
	ctx_params.max_inline_xio_data = s->msg_data_max_len;
	ctx_params.max_inline_xio_hdr = s->msg_head_max_len;

	con->xio_con_ctx = xio_context_create(&ctx_params, 0, con->client.affinity);
	LOG_THEN_RETURN_VAL_IF_TRUE(!con->xio_con_ctx, -1, "xio_context_create fail.");

	(void)memset(&xio_con_param, 0, sizeof(struct xio_connection_params));
	xio_con_param.session			= s->xio_s;
	xio_con_param.ctx				= con->xio_con_ctx;
	xio_con_param.conn_idx			= con->client.affinity;
	xio_con_param.conn_user_context	= con;

	ret = arpc_cond_lock(&con->cond);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_xio_ctx, "arpc_cond_lock fail, maybe free already.");
	
	con->xio_con = xio_connect(&xio_con_param);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!con->xio_con, unlock, "xio_connect fail.");
	thread.loop = &xio_client_run_con;
	thread.stop = NULL;
	thread.usr_ctx = (void*)con;

	con->client.thread_handle = tp_post_one_work(s->threadpool, &thread, WORK_DONE_AUTO_FREE);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!con->client.thread_handle, thread_exit, "tp_post_one_work fail.");

	ret = arpc_cond_wait_timeout(&con->cond, WAIT_THREAD_RUNING_TIMEOUT);
	arpc_cond_unlock(&con->cond);

	if(ret){
		ARPC_LOG_ERROR("thread create fail, cancel it.");
		tp_cancel_one_work(&con->client.thread_handle);
	}
	return ret;
thread_exit:
	if(con->status == XIO_STA_RUN || con->status == XIO_STA_RUN_ACTION){
		con->status = XIO_STA_CLEANUP;
		xio_context_stop_loop(con->xio_con_ctx);
		ret = arpc_cond_wait_timeout(&con->cond, WAIT_THREAD_RUNING_TIMEOUT);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_wait_timeout");
	}
unlock:
	arpc_cond_unlock(&con->cond);
free_xio_ctx:
	if(con->xio_con_ctx)
		xio_context_destroy(con->xio_con_ctx);
	return -1;
}

static int  arpc_finish_client_conn(struct arpc_connection *con)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, -1, "arpc_connection is null fail.");
	xio_client_stop_con(con);// 有锁，不要再用锁了
	if(con->xio_con_ctx)
		xio_context_destroy(con->xio_con_ctx);
	return 0;
}

struct arpc_connection *arpc_create_connection(enum arpc_connection_type type, 
										struct arpc_session_handle *s, 
										uint32_t index)
{
	struct arpc_connection *con = NULL;
	int ret;
	con = arpc_new_connection();
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, NULL, "arpc_new_connection fail.");
	switch (type)
	{
		case ARPC_CON_TYPE_CLIENT:
			ret = arpc_init_client_conn(con, s, index);
			break;
		case ARPC_CON_TYPE_SERVER:
			con->is_busy = 0;
			con->conn_mode = ARPC_CON_MODE_DIRE_IO;
			ret = 0;
			break;
		default:
			ARPC_LOG_ERROR("unkown connetion type[%d]", type);
			ret = ARPC_ERROR;
	}
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_con, "init connection fail.");
	con->id = s->conn_num;
	con->type = type;
	con->session = s;
	con->usr_ops_ctx = s->usr_context;
	con->ops = &s->ops;
	con->msg_iov_max_len = s->msg_iov_max_len;
	con->msg_data_max_len = s->msg_data_max_len;
	con->msg_head_max_len = s->msg_head_max_len;

	ret = session_insert_con(s, con);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_con, "session[%p] insert con[%u][%p] fail.", s, con->id, con);

	return con;
free_con:
	ret = arpc_delete_connection(con);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_delete_connection fail.");
	return NULL;
}

int  arpc_disconnection(struct arpc_connection *con, int64_t timeout_s)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "arpc_connection is null.");
	// 执行释放
	ret = arpc_cond_lock(&con->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	if(con->xio_con && con->status == XIO_STA_RUN_ACTION){
		ARPC_LOG_NOTICE("conn[%u][%p] will be closed.", con->id, con);
		xio_disconnect(con->xio_con);
		if(timeout_s){
			ret = arpc_cond_wait_timeout(&con->cond, timeout_s);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_wait_timeout con[%u][%p] disclosed time out.",con->id, con);
		}
		con->xio_con = NULL;
	}
	con->status = XIO_STA_CLEANUP;
	arpc_cond_notify_all(&con->cond);

	arpc_cond_unlock(&con->cond);

	return 0;
}

int  arpc_destroy_connection(struct arpc_connection *con, int64_t timeout_s)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "arpc_connection is null.");
	//通知释放
	ret = arpc_cond_lock(&con->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	arpc_cond_unlock(&con->cond);
	switch (con->type)
	{
		case ARPC_CON_TYPE_CLIENT:
			ret = arpc_finish_client_conn(con);
			break;
		case ARPC_CON_TYPE_SERVER:
			break;
		default:
			ARPC_LOG_ERROR("unkown connetion type[%d]", con->type);
			ret = -1;
			break;
	}
	ret = arpc_delete_connection(con);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_delete_connection");
	return 0;
}

static int xio_client_run_con(void * ctx)
{
	struct arpc_connection *con = (struct arpc_connection *)ctx;
	int32_t sleep_time = 0;
	cpu_set_t		cpuset;
	int ret;
	char thread_name[16+1] ={0};

	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "con null fail.");
	ARPC_LOG_DEBUG("session run on the thread[%lu].", pthread_self());

	ret = arpc_cond_lock(&con->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock con cond fail.");

	if(!con->xio_con_ctx){
		ARPC_LOG_ERROR("xio_con_ctx null...");
		arpc_cond_unlock(&con->cond);
		return ARPC_ERROR;
	}
	con->status = XIO_STA_RUN;

	CPU_ZERO(&cpuset);
	CPU_SET((con->id + 1)%(arpc_cpu_max_num()), &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	snprintf(thread_name, sizeof(thread_name), "arpc_conn_%d", con->id);
	prctl(PR_SET_NAME, thread_name);

	arpc_cond_notify(&con->cond);

	arpc_cond_unlock(&con->cond);
	for(;;){	
		if(!con->xio_con_ctx){
			break;
		}
		if (xio_context_run_loop(con->xio_con_ctx, XIO_INFINITE) < 0)
			ARPC_LOG_ERROR("xio error msg: %s.", xio_strerror(xio_errno()));
		ARPC_LOG_DEBUG("xio context run loop pause...");
		arpc_cond_lock(&con->cond);
		sleep_time = con->client.recon_interval_s;
		if(con->status >= XIO_STA_CLEANUP){
			arpc_cond_notify(&con->cond);
			break;
		}
		arpc_cond_unlock(&con->cond);
		if (sleep_time > 0) 
			arpc_sleep(sleep_time);	// 恢复周期
	}
	ARPC_LOG_NOTICE("xio connection[%p] on thread[%lu] exit now.", con,  pthread_self());
	con->status = XIO_STA_EXIT;
	arpc_cond_unlock(&con->cond);
	return ARPC_SUCCESS;
}

static void xio_client_stop_con(struct arpc_connection* con)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ;, "con null fail.");
	ret = arpc_cond_lock(&con->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ;, "tx event  arpc_cond_lock fail.");
	if(con->xio_con_ctx){
		xio_context_stop_loop(con->xio_con_ctx);
	}
	if (con->status <= XIO_STA_CLEANUP){
		ret = arpc_cond_wait_timeout(&con->cond, 5*1000);
		LOG_ERROR_IF_VAL_TRUE(ret, "wait loop exit time out.");
	}
	arpc_cond_unlock(&con->cond);
	return;
}

static void arpc_tx_event_callback(int fd, int events, void *data)
{
	struct arpc_connection *con;
	int ret;
	QUEUE* iter;
	struct arpc_common_msg *msg;
	char buf[sizeof(pipe_magic)+1] = {0};

	int max_tx_send = MAX_TX_CNT_PER_WAKEUP; //每次唤醒最多发送消息数，避免阻塞太长
	LOG_THEN_RETURN_VAL_IF_TRUE(!data, ;, "data is null.");
	con = (struct arpc_connection *)data;
	//ARPC_LOG_NOTICE("work thread sync send, thread[%lu]", pthread_self());
	ret = read(fd, buf, sizeof(pipe_magic));//避免阻塞
	LOG_ERROR_IF_VAL_TRUE(ret < 0, "read fd[%d] fail, ret[%d]", fd, ret);
	/*if(ret > 0){
		ARPC_LOG_NOTICE("read buf[%s]", buf);
	}*/
	ret = arpc_cond_lock(&con->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ;, "tx event  arpc_cond_lock fail.");
	while(max_tx_send){
		if(QUEUE_EMPTY(&con->q_tx_msg)){
			con->tx_msg_num = 0;
			break;
		}
		iter = QUEUE_HEAD(&con->q_tx_msg);
		msg = QUEUE_DATA(iter, struct arpc_common_msg, q);
		QUEUE_REMOVE(iter);
		QUEUE_INIT(iter);
		if(msg->magic != ARPC_COM_MSG_MAGIC){
			ARPC_LOG_ERROR("unkown msg");
			con->tx_msg_num--;
			continue;
		}
		ret = arpc_cond_lock(&msg->cond);
		if(ret){
			ARPC_LOG_ERROR("arpc_cond_lock msg fail, maybe delete");
			con->tx_msg_num--;
			continue;
		}
		
		switch (msg->type)
		{
			case ARPC_MSG_TYPE_REQ:
				ret = xio_send_request(con->xio_con, msg->tx_msg);
				break;
			case ARPC_MSG_TYPE_RSP:
				ret = xio_send_response(msg->tx_msg);
				break;
			case ARPC_MSG_TYPE_OW:
				ret = xio_send_msg(con->xio_con, msg->tx_msg);
				break;
			default:
				ret = ARPC_ERROR;
				ARPC_LOG_ERROR("unkown msg type[%d]", msg->type);
				break;
		}
		if(ret){
			ARPC_LOG_ERROR("send msg[%d] fail, errno code[%u], err msg[%s].", msg->type, xio_errno(), xio_strerror(xio_errno()));
			if(msg->retry_cnt > 3){
				ARPC_LOG_ERROR("retry cnt[%u], retry overload, will remove it.", msg->retry_cnt);
				QUEUE_INSERT_TAIL(&con->q_tx_end, iter);//失败消息移到已发送的队列
			}else{
				ARPC_LOG_ERROR("retry cnt[%u], retry later.", msg->retry_cnt);
				msg->retry_cnt++;
				QUEUE_INSERT_TAIL(&con->q_tx_msg, iter);//移到发送的队列尾部
			}
			arpc_cond_unlock(&msg->cond);
			continue;
		}
		msg->retry_cnt = 0;
		QUEUE_INSERT_TAIL(&con->q_tx_end, iter);//移到已发送的队列
		arpc_cond_unlock(&msg->cond);
		con->tx_msg_num--;
		con->tx_end_num++;
		max_tx_send--;
	}
	arpc_cond_notify_all(&con->cond);//唤醒多个线程
	arpc_cond_unlock(&con->cond);

	return;
}

int  arpc_add_tx_event_to_conn(struct arpc_connection *con)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "arpc_connection is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!con->xio_con_ctx, ARPC_ERROR, "xio_con_ctx is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(con->pipe_fd[0] < 0, ARPC_ERROR, "pipe_fd[1] is invalid.");
	//注册写事件
	ret = xio_context_add_ev_handler(con->xio_con_ctx, con->pipe_fd[0], XIO_POLLIN|XIO_POLLET, arpc_tx_event_callback, con);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "xio_context_add_ev_handler fail.");
	return 0;
}

int  arpc_del_tx_event_to_conn(struct arpc_connection *con)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!con, ARPC_ERROR, "arpc_connection is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!con->xio_con_ctx, ARPC_ERROR, "xio_con_ctx is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(con->pipe_fd[0] < 0, ARPC_ERROR, "pipe_fd[1] is invalid.");
	//注册写方法
	ret = xio_context_del_ev_handler(con->xio_con_ctx, con->pipe_fd[0]);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "xio_context_add_ev_handler fail.");
	return 0;
}

// server
struct arpc_server_handle *arpc_create_server(uint32_t ex_ctx_size)
{
	int ret = 0;
	int i =0;
	struct arpc_server_handle *svr = NULL;
	/* handle*/
	svr = (struct arpc_server_handle *)ARPC_MEM_ALLOC(sizeof(struct arpc_server_handle) + ex_ctx_size, NULL);
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

struct arpc_work_handle *arpc_create_xio_server_work(const struct arpc_con_info *con_param, 
													struct arpc_server_handle* server, 
													struct xio_session_ops *work_ops,
													uint32_t index)
{
	struct arpc_work_handle *work;
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

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(work->status != XIO_WORK_STA_RUN, cancel_work, "work run status[%d] fail.", work->status);
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

int  arpc_destroy_xio_server_work(struct arpc_work_handle *work)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, -1, "arpc_work_handle is null fail.");

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

int server_insert_work(struct arpc_server_handle *server, struct arpc_work_handle *work)
{
	int ret;
	ret = arpc_mutex_lock(&server->lock);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_mutex_lock server[%p] fail.", server);
	QUEUE_INSERT_TAIL(&server->q_work, &work->q);
	server->work_num++;
	arpc_mutex_unlock(&server->lock);
	return 0;
}

int server_remove_work(struct arpc_server_handle *server, struct arpc_work_handle *work)
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
static struct arpc_work_handle *arpc_create_work()
{
	int ret = 0;
	struct arpc_work_handle *work = NULL;
	
	/* handle*/
	work = (struct arpc_work_handle *)ARPC_MEM_ALLOC(sizeof(struct arpc_work_handle), NULL);
	if (!work) {
		ARPC_LOG_ERROR( "malloc error, exit ");
		return NULL;
	}
	memset(work, 0, sizeof(struct arpc_work_handle));

	ret = arpc_cond_init(&work->cond); 
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, NULL, "arpc_cond_init fail.");
	QUEUE_INIT(&work->q);
	work->magic = ARPC_WROK_MAGIC;
	return work;
}

static void destroy_work_handle(QUEUE* work_q)
{
	struct arpc_work_handle *work_handle;
	int ret;

	LOG_THEN_RETURN_VAL_IF_TRUE(!work_q, ;, "work_q null.");
	work_handle = QUEUE_DATA(work_q, struct arpc_work_handle, q);
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

static int arpc_destroy_work(struct arpc_work_handle* work)
{
	int ret = 0;
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, ARPC_ERROR, "work null.");
	ret = arpc_cond_destroy(&work->cond); 
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_destroy fail.");
	SAFE_FREE_MEM(work);
	return 0;
}

static int xio_server_work_run(void * ctx)
{
	struct arpc_work_handle *work = (struct arpc_work_handle *)ctx;
	cpu_set_t		cpuset;
	char thread_name[16+1] ={0};
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, ARPC_ERROR, "work null fail.");
	ARPC_LOG_DEBUG("work run on the thread[%lu].", pthread_self());
	arpc_cond_lock(&work->cond);
	work->status = XIO_WORK_STA_RUN;

	snprintf(thread_name, sizeof(thread_name), "arpc_work_%d", work->affinity);
	prctl(PR_SET_NAME, thread_name);

	CPU_ZERO(&cpuset);
	CPU_SET((work->affinity)%(arpc_cpu_max_num()), &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	arpc_cond_notify(&work->cond);
	arpc_cond_unlock(&work->cond);
	for(;;){	
		if (xio_context_run_loop(work->work_ctx, XIO_INFINITE) < 0)
			ARPC_LOG_ERROR("xio error msg: %s.", xio_strerror(xio_errno()));
		ARPC_LOG_DEBUG("xio context run loop pause...");
		arpc_cond_lock(&work->cond);
		if(work->status == XIO_WORK_STA_EXIT){
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
	struct arpc_work_handle *work = (struct arpc_work_handle *)ctx;
	LOG_THEN_RETURN_VAL_IF_TRUE(!work, ;, "work null fail.");
	work->status = XIO_WORK_STA_EXIT;
	if(work->work_ctx)
		xio_context_stop_loop(work->work_ctx);
	return;
}
int check_xio_msg_valid(const struct arpc_connection *conn, const struct xio_vmsg *pmsg)
{
	uint32_t iov_depth = 0;
	uint32_t nents = 0;
	LOG_THEN_RETURN_VAL_IF_TRUE(!pmsg, ARPC_ERROR, "tx_msg null.");
	
	if(pmsg->header.iov_len && !pmsg->header.iov_base){
		ARPC_LOG_ERROR("header invalid, iov_len:%lu, iov_base:%p", 
						pmsg->header.iov_len, 
						pmsg->header.iov_base);
		return -1;
	}

	if(pmsg->header.iov_len > conn->msg_head_max_len){
		ARPC_LOG_ERROR("header len over limit, head len:%lu, max:%u.", 
						pmsg->header.iov_len, 
						conn->msg_head_max_len);
		return -1;
	}

	if (pmsg->total_data_len) {
		LOG_THEN_RETURN_VAL_IF_TRUE((conn->msg_data_max_len < pmsg->total_data_len), -1, 
									"data len over limit, msg len:%lu, max:%lu.", 
									pmsg->total_data_len, 
									conn->msg_data_max_len);
	}
	nents = vmsg_sglist_nents(pmsg);
	if(nents > (conn->msg_data_max_len/conn->msg_iov_max_len)){
		ARPC_LOG_ERROR("data depth over limit, nents:%u, max:%lu.", 
						nents, 
						(conn->msg_data_max_len/conn->msg_iov_max_len));
		return -1;
	}
	return 0;
}
int arpc_session_async_send(struct arpc_connection *conn, struct arpc_common_msg  *msg)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!conn, ARPC_ERROR, "arpc_connection null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "arpc_conn_ow_msg null.");

	ret = arpc_cond_lock(&conn->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((conn->status != XIO_STA_RUN_ACTION), unlock, "connoection on cleaning up");

	ret = check_xio_msg_valid(conn, &msg->tx_msg->out);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "check msg invalid.");

	while((conn->tx_msg_num > ARPC_CONN_TX_MAX_DEPTH) && (conn->status == XIO_STA_RUN_ACTION)){
		ARPC_LOG_NOTICE("con is busy, tx_msg_num[%lu] wait release,", conn->tx_msg_num);
		ret = arpc_cond_wait_timeout(&conn->cond, 1 + 2*conn->tx_msg_num);
		LOG_ERROR_IF_VAL_TRUE(ret, "conn[%u][%p], xio con[%p] wait timeout to coninue.", conn->id, conn, conn->xio_con);
		if (ret){
			continue;
		}
		ret = 0;
		break;
	}
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "connoection invalid, send msg[%d] fail.", msg->type);

	conn->tx_msg_num++;
	QUEUE_INIT(&msg->q);
	QUEUE_INSERT_TAIL(&conn->q_tx_msg, &msg->q);
	msg->ref++;//引用计数
	//ARPC_LOG_NOTICE("insert msg[%p].............", msg);
	ret = write(conn->pipe_fd[1], pipe_magic, sizeof(pipe_magic));//触发发送事件
	LOG_ERROR_IF_VAL_TRUE(ret < 0, "ret[%d], write fd[%d] fail", ret, conn->pipe_fd[1]);

	arpc_cond_unlock(&conn->cond);
	return 0;
unlock:
	arpc_cond_unlock(&conn->cond);
	return -1;
}

int arpc_session_send_comp_notify(struct arpc_connection *conn, struct arpc_common_msg *msg)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!conn, ARPC_ERROR, "arpc_connection null.");

	ret = arpc_cond_lock(&conn->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((conn->status == XIO_STA_CLEANUP), unlock, "connoection on cleaning up");

	ret = arpc_cond_lock(&msg->cond);
	if(!ret){
		QUEUE_REMOVE(&msg->q);
		QUEUE_INIT(&msg->q);
		msg->ref--;
		arpc_cond_unlock(&msg->cond);
	}else{
		ARPC_LOG_ERROR(" lock msg fail, maybe free...");
	}
	if(conn->tx_end_num){
		conn->tx_end_num--;
	}
	arpc_cond_notify(&conn->cond);
	arpc_cond_unlock(&conn->cond);
	return 0;
unlock:
	arpc_cond_unlock(&conn->cond);
	return -1;
}

int arpc_get_session_opt(const arpc_session_handle_t *ses, struct aprc_session_opt *opt)
{
	struct arpc_session_handle *s = (struct arpc_session_handle *)ses;
	LOG_THEN_RETURN_VAL_IF_TRUE(!ses, ARPC_ERROR, "ses null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!opt, ARPC_ERROR, "opt null.");
	opt->msg_data_max_len = s->msg_data_max_len;
	opt->msg_head_max_len = s->msg_head_max_len;
	opt->msg_iov_max_len = s->msg_iov_max_len;
	return 0;
}