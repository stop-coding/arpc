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
#include <sys/eventfd.h>

#include "arpc_connection.h"

#define WAIT_THREAD_RUNING_TIMEOUT (1000)

#define ARPC_EVENT_BUF_MAX_LEN 	4

#define ARPC_CONN_TX_MAX_DEPTH	  		500
#define ARPC_MAX_TX_CNT_PER_WAKEUP		32
#define COMM_MSG_GROW_STEP				64

#define ARPC_CONN_EXIT_MAX_TIMES_MS	(2*1000)

#define ARPC_CONN_ATTR_TELL_LIVE  (1<<0)
#define ARPC_CONN_ATTR_ADD_EVENT  (1<<1)
#define ARPC_CONN_ATTR_REBUILD    (1<<2)
#define ARPC_CONN_ATTR_EXIT       (1<<3)
#define ARPC_CONN_ATTR_SET_EXIT   (1<<4)

#define CONN_CTX(ctx_name, obj, ret)\
struct arpc_connection_ctx *ctx_name;\
LOG_THEN_RETURN_VAL_IF_TRUE(!obj, ret, "obj is null");\
ctx_name = (struct arpc_connection_ctx *)obj->ctx;

#define ARPC_CLIENT_CTX(ctx, session_fd, usr_ctx)	\
struct arpc_client_ctx *ctx = NULL;\
struct arpc_session_handle *session_fd = (struct arpc_session_handle *)usr_ctx;\
do{\
	if(session_fd && session_fd->ex_ctx)\
		ctx = (struct arpc_client_ctx *)session_fd->ex_ctx;\
}while(0);

enum arpc_connection_status{
	ARPC_CON_STA_INIT = 0, 	 //初始化
	ARPC_CON_STA_RUN, 		//运行
	ARPC_CON_STA_RUN_ACTIVE, //活跃，指链路联通
	ARPC_CON_STA_TEARDOWN,
	ARPC_CON_STA_DISCONNECTED, 	//关闭
	ARPC_CON_STA_CLEANUP, 	//释放
	ARPC_CON_STA_EXIT, 		//退出
};

enum arpc_connection_mode{
	ARPC_CON_MODE_DIRE_IO = 0,
	ARPC_CON_MODE_DIRE_OUT, //
	ARPC_CON_MODE_DIRE_IN,  //
};

enum arpc_conn_event{
	ARPC_CONN_EVENT_E_NONE = 0,
	ARPC_CONN_EVENT_E_DISCONNECT = 1,
	ARPC_CONN_EVENT_E_SEND_DATA, //
	ARPC_CONN_EVENT_E_STOP_LOOP,  //
};

struct arpc_con_client {
	int32_t						affinity;				/* 绑定CPU */
	int32_t						recon_interval_s;
};

struct arpc_connection_ctx {
	uint32_t					magic;
	uint32_t					msg_head_max_len;
	uint64_t					msg_data_max_len;
	uint32_t					msg_iov_max_len;
	struct xio_connection		*xio_con;				/* connection 资源*/
	struct xio_context			*xio_con_ctx;			/* context 资源*/
	struct arpc_session_handle  *session;
	enum arpc_io_type			io_type;
	enum arpc_connection_type   type;				
	enum arpc_connection_status	status;
	void						*usr_ctx;
	uint32_t					flags;
	uint8_t						is_busy;
	enum arpc_connection_mode	conn_mode;
	struct arpc_cond 			cond;
	struct arpc_rwlock 			rwlock;
	int 						event_fd;
	uint64_t					event_cnt;
	uint64_t					tx_msg_num;
	uint32_t					busy_msg;
	struct arpc_mutex 			msg_lock;			/* 消息锁*/
	QUEUE     					q_tx_msg;			/* 待发送队列*/							
	QUEUE     					q_used_msg;			/* 被申请的队列*/
	QUEUE     					q_req_msg;			/* 空闲队列*/
	QUEUE     					q_ow_msg;
	QUEUE     					q_rsp_msg;
	int64_t						conn_timeout_ms;					
};

static void check_session_active(struct arpc_connection_ctx *ctx);
static int  arpc_client_run_loop(void * thread_ctx);
static int  arpc_add_event_to_conn(struct arpc_connection *con);
static int  arpc_del_event_to_conn(struct arpc_connection *con);
static void arpc_tx_event_callback(struct arpc_connection *usr_conn);
static struct arpc_common_msg *arpc_create_common_msg(uint32_t ex_data_size);
static int arpc_destroy_common_msg(struct arpc_common_msg *msg);

struct arpc_connection *arpc_create_connection(const struct arpc_connection_param *param)
{
	int ret = 0;
	int flags;
	struct arpc_connection *con = NULL;
	struct arpc_connection_ctx *ctx = NULL;

	LOG_THEN_RETURN_VAL_IF_TRUE(!param, NULL, "arpc_connection_param is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!param->session, NULL, "session is null.");
	/* handle*/
	con = (struct arpc_connection *)arpc_mem_alloc(sizeof(struct arpc_connection) + sizeof(struct arpc_connection_ctx), NULL);
	if (!con) {
		ARPC_LOG_ERROR( "malloc error, exit ");
		return NULL;
	}
	memset(con, 0, sizeof(struct arpc_connection) + sizeof(struct arpc_connection_ctx));
	con->id = param->id;
	QUEUE_INIT(&con->q);

	ctx = (struct arpc_connection_ctx*)con->ctx;

	ret = arpc_cond_init(&ctx->cond); 
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_buf, "arpc_cond_init fail.");

	ret = arpc_rwlock_init(&ctx->rwlock); 
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_buf, "arpc_rwlock_init fail.");

	ret = arpc_mutex_init(&ctx->msg_lock); 
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_buf, "arpc_mutex_init fail.");

	ctx->event_fd = eventfd(0, EFD_NONBLOCK);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ctx->event_fd == -1, free_cond, "eventfd init fail.");

	QUEUE_INIT(&ctx->q_tx_msg);
	QUEUE_INIT(&ctx->q_used_msg);

	QUEUE_INIT(&ctx->q_req_msg);
	QUEUE_INIT(&ctx->q_rsp_msg);
	QUEUE_INIT(&ctx->q_ow_msg);

	ctx->magic = ARPC_CONN_MAGIC;
	ctx->usr_ctx = param->usr_ctx;
	ctx->conn_timeout_ms = param->timeout_ms;

	ctx->status = ARPC_CON_STA_INIT;
	ctx->io_type = param->io_type;
	ctx->type = param->type;
	ctx->session = param->session;
	ctx->msg_iov_max_len = param->session->msg_iov_max_len;
	ctx->msg_data_max_len = param->session->msg_data_max_len;
	ctx->msg_head_max_len = param->session->msg_head_max_len;
	ctx->xio_con_ctx = param->xio_con_ctx;
	ctx->xio_con = param->xio_con;
	
	return con;
free_cond:
	arpc_cond_destroy(&ctx->cond);
free_buf:
	SAFE_FREE_MEM(con);
	return NULL;
}

int arpc_destroy_connection(struct arpc_connection *con)
{
	int ret;
	QUEUE* iter;
	struct arpc_common_msg *msg;
	CONN_CTX(ctx, con, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	ARPC_LOG_DEBUG("connection[%u] status:%d, type:%d",con->id, ctx->status, ctx->type);
	arpc_rwlock_wrlock(&ctx->rwlock);
	switch (ctx->type)
	{
		case ARPC_CON_TYPE_CLIENT:
			if (ctx->status != ARPC_CON_STA_EXIT) {
				SET_FLAG(ctx->flags, ARPC_CONN_ATTR_EXIT);
				if(ctx->xio_con_ctx){
					ARPC_LOG_DEBUG("conn[%u][%p] ctx will be stop.", con->id, con);
					xio_context_stop_loop(ctx->xio_con_ctx);
				}
				ret = arpc_cond_wait_timeout(&ctx->cond, ARPC_CONN_EXIT_MAX_TIMES_MS);
				LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "con status[%d] forbid destroy.", ctx->status);
			}
			break;
		case ARPC_CON_TYPE_SERVER:
			break;
		default:
			ARPC_LOG_ERROR("unkown connetion type[%d]", ctx->type);
			ret = -1;
			break;
	}
	
	if (ctx->event_fd >= 0) {
		close(ctx->event_fd);
	}

	// 清理空闲消息
	arpc_mutex_lock(&ctx->msg_lock);
	while(!QUEUE_EMPTY(&ctx->q_req_msg)){
		iter = QUEUE_HEAD(&ctx->q_req_msg);
		msg = QUEUE_DATA(iter, struct arpc_common_msg, q);
		QUEUE_REMOVE(iter);
		ret = arpc_destroy_common_msg(msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_connection fail.");
	}

	while(!QUEUE_EMPTY(&ctx->q_rsp_msg)){
		iter = QUEUE_HEAD(&ctx->q_rsp_msg);
		msg = QUEUE_DATA(iter, struct arpc_common_msg, q);
		QUEUE_REMOVE(iter);
		ret = arpc_destroy_common_msg(msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_connection fail.");
	}

	while(!QUEUE_EMPTY(&ctx->q_ow_msg)){
		iter = QUEUE_HEAD(&ctx->q_ow_msg);
		msg = QUEUE_DATA(iter, struct arpc_common_msg, q);
		QUEUE_REMOVE(iter);
		ret = arpc_destroy_common_msg(msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_connection fail.");
	}

	// 清理使用中的消息
	while(!QUEUE_EMPTY(&ctx->q_used_msg)){
		iter = QUEUE_HEAD(&ctx->q_used_msg);
		msg = QUEUE_DATA(iter, struct arpc_common_msg, q);
		QUEUE_REMOVE(iter);
		ret = arpc_destroy_common_msg(msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_connection fail.");
	}

	// 清理发送中的消息
	while(!QUEUE_EMPTY(&ctx->q_tx_msg)){
		iter = QUEUE_HEAD(&ctx->q_tx_msg);
		msg = QUEUE_DATA(iter, struct arpc_common_msg, q);
		QUEUE_REMOVE(iter);
		ret = arpc_destroy_common_msg(msg);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_connection fail.");
	}
	arpc_mutex_unlock(&ctx->msg_lock);
	arpc_mutex_destroy(&ctx->msg_lock);


	ctx->magic = 0;
	arpc_rwlock_unlock(&ctx->rwlock);
	arpc_rwlock_destroy(&ctx->rwlock);

	arpc_cond_unlock(&ctx->cond);
	ret = arpc_cond_destroy(&ctx->cond); 
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_destroy fail.");
	SAFE_FREE_MEM(con);
	return 0;
unlock:
	arpc_cond_unlock(&ctx->cond);
	return ARPC_ERROR;
}

static struct arpc_common_msg *arpc_create_common_msg(uint32_t ex_data_size)
{
	struct arpc_common_msg *req_msg = NULL;
	int ret;

	req_msg = (struct arpc_common_msg*)arpc_mem_alloc(sizeof(struct arpc_common_msg) + ex_data_size,NULL);
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, NULL, "arpc_mem_alloc arpc_msg fail.");
	memset(req_msg, 0, sizeof(struct arpc_common_msg) + ex_data_size);
	ret = arpc_cond_init(&req_msg->cond); 
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, error, "arpc_cond_init for new msg fail.");
	req_msg->flag = 0;
	QUEUE_INIT(&req_msg->q);
	req_msg->magic = ARPC_COM_MSG_MAGIC;
	req_msg->status = ARPC_MSG_STATUS_IDLE;
	return req_msg;
error:
	SAFE_FREE_MEM(req_msg);
	return NULL;
}


static int arpc_destroy_common_msg(struct arpc_common_msg *msg)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");
	ret = arpc_cond_lock(&msg->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock null.");
	msg->status = ARPC_MSG_STATUS_FREE;
	arpc_cond_notify(&msg->cond);
	arpc_cond_unlock(&msg->cond);
	arpc_cond_destroy(&msg->cond);
	SAFE_FREE_MEM(msg);
	return 0;
}

int arpc_client_connect(struct arpc_connection *con, int64_t timeout_ms)
{
	int ret;
	work_handle_t 	thread_handle;
	struct tp_thread_work thread;
	CONN_CTX(ctx, con, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	ctx->status = ARPC_CON_STA_INIT;
	thread.loop = &arpc_client_run_loop;
	thread.stop = NULL;
	thread.usr_ctx = (void*)con;

	thread_handle = tp_post_one_work(arpc_get_threadpool(), &thread, WORK_DONE_AUTO_FREE);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!thread_handle, unlock, "tp_post_one_work fail.");

	ret = arpc_cond_wait_timeout(&ctx->cond, timeout_ms);
	if(ret || ctx->status != ARPC_CON_STA_RUN){
		ARPC_LOG_ERROR("connect task run fail, conn[%u] status[%d].", con->id, ctx->status);
		goto unlock;
	}
	arpc_cond_unlock(&ctx->cond);
	return ret;

unlock:
	arpc_cond_unlock(&ctx->cond);
	return -1;
}

int arpc_client_wait_connected(struct arpc_connection *con, int64_t timeout_ms)
{
	int ret;
	int retry = 1;
	CONN_CTX(ctx, con, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	if(ctx->status == ARPC_CON_STA_RUN_ACTIVE) {
		arpc_cond_unlock(&ctx->cond);
		return 0;
	}

	while(retry--){
		ret = arpc_cond_wait_timeout(&ctx->cond, timeout_ms);
		if(ctx->status == ARPC_CON_STA_RUN_ACTIVE){
			break;
		}
		if(ret){
			ARPC_LOG_ERROR("wait connected timeout, conn[%u] status[%d].", con->id, ctx->status);
			break;
		}
	}
	arpc_cond_unlock(&ctx->cond);
	return ret;
}

int  arpc_client_disconnect(struct arpc_connection *con, int64_t timeout_s)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);
	// 执行释放
	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	if(ctx->xio_con && ctx->status == ARPC_CON_STA_RUN_ACTIVE){
		ARPC_LOG_DEBUG("conn[%u][%p] will be closed.", con->id, con);
		ctx->status = ARPC_CON_STA_CLEANUP;
		ret = eventfd_write(ctx->event_fd, ARPC_CONN_EVENT_E_DISCONNECT);//触发发送事件
		LOG_ERROR_IF_VAL_TRUE(ret < 0, "ret[%d], write fd[%d] fail", ret, ctx->event_fd);
	}
	arpc_cond_unlock(&ctx->cond);

	return 0;
}

// 线程安全
int arpc_client_conn_stop_loop_r(struct arpc_connection *con)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);
	// 执行释放
	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	ARPC_LOG_DEBUG("conn[%u][%p] thread will exit.", con->id, con);
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_EXIT|ARPC_CONN_ATTR_SET_EXIT);
	if(ctx->xio_con){
		ARPC_LOG_DEBUG("conn[%u][%p] ctx will be stop.", con->id, con);
		ret = eventfd_write(ctx->event_fd, ARPC_CONN_EVENT_E_STOP_LOOP);//触发发送事件
		LOG_ERROR_IF_VAL_TRUE(ret < 0, "ret[%d], write fd[%d] fail", ret, ctx->event_fd);
	}
	arpc_cond_unlock(&ctx->cond);
	return 0;
}

//非线程安全
int arpc_client_conn_stop_loop(struct arpc_connection *con)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);
	// 执行释放
	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	ARPC_LOG_DEBUG("conn[%u][%p] thread will exit.", con->id, con);
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_EXIT);
	if(ctx->xio_con_ctx){
		xio_context_stop_loop(ctx->xio_con_ctx);
	}
	arpc_cond_unlock(&ctx->cond);
	return 0;
}

int arpc_client_reconnect(struct arpc_connection *con)
{
	int ret;
	struct xio_connection_params xio_con_param;
	CONN_CTX(ctx, con, ARPC_ERROR);
	// 执行释放
	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	ctx->status = ARPC_CON_STA_RUN;
	ARPC_LOG_ERROR("conn[%u][%p] reconnection, thread ctx:%p", con->id, con, ctx->xio_con_ctx);
	(void)memset(&xio_con_param, 0, sizeof(struct xio_connection_params));
	xio_con_param.session			= ctx->session->xio_s;
	xio_con_param.ctx				= ctx->xio_con_ctx;
	xio_con_param.conn_idx			= con->id;
	xio_con_param.conn_user_context	= con;
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_REBUILD);
	ctx->xio_con = xio_connect(&xio_con_param);
	LOG_ERROR_IF_VAL_TRUE(!ctx->xio_con, "xio_connect fail.");
	if(ctx->xio_con_ctx){
		xio_context_stop_loop(ctx->xio_con_ctx);
	}
	arpc_cond_unlock(&ctx->cond);

	return 0;
}

int arpc_set_connect_status(struct arpc_connection *con)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	ret = arpc_add_event_to_conn(con);
	ctx->status = ARPC_CON_STA_RUN_ACTIVE;
	arpc_cond_notify(&ctx->cond);
	arpc_cond_unlock(&ctx->cond);
	return 0;
}

int arpc_set_disconnect_status(struct arpc_connection *con)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	ret = arpc_del_event_to_conn(con);
	ctx->status = ARPC_CON_STA_DISCONNECTED;
	ctx->xio_con = NULL;
	arpc_cond_notify(&ctx->cond);
	arpc_cond_unlock(&ctx->cond);
	return 0;
}

int arpc_lock_connection(struct arpc_connection *con)
{
	CONN_CTX(ctx, con, ARPC_ERROR);
	return arpc_cond_lock(&ctx->cond);
}

int arpc_unlock_connection(struct arpc_connection *con)
{
	CONN_CTX(ctx, con, ARPC_ERROR);
	return arpc_cond_unlock(&ctx->cond);
}

static int arpc_connect_init(struct arpc_connection *con)
{
	int ret;
	struct xio_connection_params xio_con_param;
	struct xio_context_params ctx_params;
	CONN_CTX(ctx, con, ARPC_ERROR);

	(void)memset(&ctx_params, 0, sizeof(struct xio_context_params));
	ctx_params.max_inline_xio_data = ctx->msg_data_max_len;
	ctx_params.max_inline_xio_hdr = ctx->msg_head_max_len;

	ctx->xio_con_ctx = xio_context_create(&ctx_params, 0, (con->id + 1));
	LOG_THEN_RETURN_VAL_IF_TRUE(!ctx->xio_con_ctx, ARPC_ERROR, "xio_context_create fail.");

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ctx->session, free_xio_ctx, "session is null.");
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ctx->session->xio_s, free_xio_ctx, "xio session is null.");

	(void)memset(&xio_con_param, 0, sizeof(struct xio_connection_params));
	xio_con_param.session			= ctx->session->xio_s;
	xio_con_param.ctx				= ctx->xio_con_ctx;
	xio_con_param.conn_idx			= con->id + 1;
	xio_con_param.conn_user_context	= con;

	ctx->xio_con = xio_connect(&xio_con_param);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!ctx->xio_con, free_xio_ctx, "xio_connect fail.");

	return 0;
free_xio_ctx:
	if(ctx->xio_con_ctx)
		xio_context_destroy(ctx->xio_con_ctx);
	ctx->xio_con_ctx = NULL;
	return -1;
}

static int arpc_client_run_loop(void * thread_ctx)
{
	struct arpc_connection *con = (struct arpc_connection *)thread_ctx;
	//cpu_set_t		cpuset;
	int ret;
	char thread_name[16+1] ={0};
	int64_t time_out_ms = XIO_INFINITE;
	CONN_CTX(ctx, con, ARPC_ERROR);

	LOG_THEN_RETURN_VAL_IF_TRUE(ctx->magic != ARPC_CONN_MAGIC, -1, "magic error.");

	ARPC_LOG_DEBUG("connection run on the thread[%lu].", pthread_self());

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock con cond fail.");
	ctx->status = ARPC_CON_STA_RUN;
	ret = arpc_connect_init(con);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, exit_thread, "arpc_connect_init fail.");

	/*CPU_ZERO(&cpuset);
	CPU_SET((con->id + 1)%(arpc_cpu_max_num()), &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);*/

	snprintf(thread_name, sizeof(thread_name), "arpc_conn_%d", con->id);
	prctl(PR_SET_NAME, thread_name);

	arpc_cond_notify(&ctx->cond);
	ARPC_LOG_DEBUG("conn_timeout_ms[%ld] timeout.", ctx->conn_timeout_ms);
	if(ctx->conn_timeout_ms > 0) {
		time_out_ms = ctx->conn_timeout_ms;
	}
	for(;;){
		arpc_cond_unlock(&ctx->cond);
		ret = xio_context_run_loop(ctx->xio_con_ctx, time_out_ms);
		if (ret) {
			ARPC_LOG_ERROR("xio loop error msg: %s.", xio_strerror(xio_errno()));
		}

		check_session_active(ctx);

		ret = arpc_cond_lock(&ctx->cond);
		LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock con cond fail.");

		if (IS_SET(ctx->flags, ARPC_CONN_ATTR_EXIT)) {
			if (ctx->xio_con) {
				xio_disconnect(ctx->xio_con);
				ctx->xio_con = NULL;
				time_out_ms = ARPC_CONN_EXIT_MAX_TIMES_MS/8;
				continue;
			}else{
				CLR_FLAG(ctx->flags, ARPC_CONN_ATTR_EXIT);
				break;
			}
		}
		ARPC_LOG_NOTICE("xio connection[%u] loop continue, timeout[%ld ms], flags[0x%x].", con->id, time_out_ms, ctx->flags);
	}

exit_thread:
	ctx->status = ARPC_CON_STA_EXIT;
	if (ctx->xio_con_ctx) {
		xio_context_destroy(ctx->xio_con_ctx);
		ctx->xio_con_ctx = NULL;
	}
	ctx->flags = 0;
	prctl(PR_SET_NAME, "share_thread");
	arpc_cond_notify(&ctx->cond);
	ARPC_LOG_NOTICE("xio connection[%u] on thread[%lu] exit now.", con->id,  pthread_self());
	arpc_cond_unlock(&ctx->cond);
	return ARPC_SUCCESS;
}

static void arpc_conn_event_callback(int fd, int events, void *data)
{
	struct arpc_connection *con = (struct arpc_connection *)data;
	int ret;
	int type = 0;
	eventfd_t count = 0;
	CONN_CTX(ctx, con, ;);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ;, "tx event  arpc_cond_lock fail.");
	if (IS_SET(ctx->flags, ARPC_CONN_ATTR_EXIT|ARPC_CONN_ATTR_SET_EXIT)){
		ARPC_LOG_NOTICE("conn context[%d] to exit", ctx->status);
		if(ctx->xio_con_ctx){
			ARPC_LOG_NOTICE("xio stop loop: %p.", ctx->xio_con_ctx);
			xio_context_stop_loop(ctx->xio_con_ctx);
		}
		arpc_cond_unlock(&ctx->cond);
		return;
	}
	arpc_cond_unlock(&ctx->cond);
	switch (ctx->status)
	{
	case ARPC_CON_STA_CLEANUP:
		ARPC_LOG_NOTICE("conn status[%d] to disconnect", ctx->status);
		if (ctx->xio_con) {
			ret = xio_disconnect(ctx->xio_con);
			if (ret) {
				ARPC_LOG_ERROR("xio disconnect error msg: %s.", xio_strerror(xio_errno()));
			}
		}
		break;
	case ARPC_CON_STA_RUN_ACTIVE:
		arpc_tx_event_callback(con);//发送事件
		break;
	default:
		ARPC_LOG_ERROR("conn status[%d] unkown", ctx->status);
		break;
	}
	
	return;
}
static void arpc_tx_event_callback(struct arpc_connection *usr_conn)
{
	int ret;
	QUEUE* iter;
	int retry = 0;
	struct arpc_common_msg *msg;
	int max_tx_send = ARPC_MAX_TX_CNT_PER_WAKEUP; //每次唤醒最多发送消息数，避免阻塞太长
	CONN_CTX(con, usr_conn, ;);

	while(max_tx_send){
		arpc_mutex_lock(&con->msg_lock);
		if(QUEUE_EMPTY(&con->q_tx_msg)){
			con->tx_msg_num = 0;
			arpc_mutex_unlock(&con->msg_lock);
			break;
		}
		iter = QUEUE_HEAD(&con->q_tx_msg);
		msg = QUEUE_DATA(iter, struct arpc_common_msg, q);
		if(msg->magic != ARPC_COM_MSG_MAGIC){
			ARPC_LOG_ERROR("unkown msg");
			QUEUE_REMOVE(iter);
			QUEUE_INIT(iter);
			con->tx_msg_num--;
			arpc_mutex_unlock(&con->msg_lock);
			continue;
		}
		arpc_mutex_unlock(&con->msg_lock);
		ARPC_LOG_TRACE("xio send msg on client, msg type:%d", msg->type);
		switch (msg->type)
		{
			case ARPC_MSG_TYPE_REQ:
				usr_conn->tx_req_count++;
				ret = xio_send_request(con->xio_con, msg->tx_msg);
				break;
			case ARPC_MSG_TYPE_RSP:
				usr_conn->tx_rsp_count++;
				ret = xio_send_response(msg->tx_msg);
				break;
			case ARPC_MSG_TYPE_OW:
				usr_conn->tx_ow_count++;
				ret = xio_send_msg(con->xio_con, msg->tx_msg);
				break;
			default:
				ret = ARPC_ERROR;
				ARPC_LOG_ERROR("unkown msg type[%d]", msg->type);
				break;
		}
		ARPC_LOG_TRACE("xio send msg end, msg type:%d", msg->type);
		if(ret){
			ARPC_LOG_ERROR("send msg[%d] fail, errno code[%u], err msg[%s].", msg->type, xio_errno(), xio_strerror(xio_errno()));
			if(retry < 3){
				ARPC_LOG_ERROR("retry cnt[%u], retry later.", msg->retry_cnt);
				retry++;
				continue;
			}
		}
		retry = 0;
		arpc_mutex_lock(&con->msg_lock);
		QUEUE_REMOVE(&msg->q);
		QUEUE_INIT(&msg->q);
		msg->status = ARPC_MSG_STATUS_USED;
		QUEUE_INSERT_TAIL(&con->q_used_msg, &msg->q);
		con->tx_msg_num--;
		arpc_mutex_unlock(&con->msg_lock);
		max_tx_send--;
	}

	return;
}

static int  arpc_add_event_to_conn(struct arpc_connection *con)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);
	LOG_THEN_RETURN_VAL_IF_TRUE(!ctx->xio_con_ctx, ARPC_ERROR, "xio_con_ctx is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(ctx->event_fd < 0, ARPC_ERROR, "event_fd is invalid.");
	//注册写事件
	ret = xio_context_add_ev_handler(ctx->xio_con_ctx, ctx->event_fd, XIO_POLLET|XIO_POLLIN, arpc_conn_event_callback, con);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "xio_context_add_ev_handler fail.");
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_ADD_EVENT);
	return 0;
}

static int  arpc_del_event_to_conn(struct arpc_connection *con)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);
	LOG_THEN_RETURN_VAL_IF_TRUE(!ctx->xio_con_ctx, ARPC_ERROR, "xio_con_ctx is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(ctx->event_fd < 0, ARPC_ERROR, "event_fd is invalid.");
	if (IS_SET(ctx->flags, ARPC_CONN_ATTR_ADD_EVENT)){
		ret = xio_context_del_ev_handler(ctx->xio_con_ctx, ctx->event_fd);
		LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "xio_context_add_ev_handler fail.");
		CLR_FLAG(ctx->flags, ARPC_CONN_ATTR_ADD_EVENT);
	}
	return 0;
}

int check_xio_msg_valid(const struct arpc_connection *conn, const struct xio_vmsg *pmsg)
{
	uint32_t iov_depth = 0;
	uint32_t nents = 0;
	CONN_CTX(ctx, conn, ARPC_ERROR);

	LOG_THEN_RETURN_VAL_IF_TRUE(!pmsg, ARPC_ERROR, "tx_msg null.");
	
	if(pmsg->header.iov_len && !pmsg->header.iov_base){
		ARPC_LOG_ERROR("header invalid, iov_len:%lu, iov_base:%p", 
						pmsg->header.iov_len, 
						pmsg->header.iov_base);
		return -1;
	}

	if(pmsg->header.iov_len > ctx->msg_head_max_len){
		ARPC_LOG_ERROR("header len over limit, head len:%lu, max:%u.", 
						pmsg->header.iov_len, 
						ctx->msg_head_max_len);
		return -1;
	}

	if (pmsg->total_data_len) {
		LOG_THEN_RETURN_VAL_IF_TRUE((ctx->msg_data_max_len < pmsg->total_data_len), -1, 
									"data len over limit, msg len:%lu, max:%lu.", 
									pmsg->total_data_len, 
									ctx->msg_data_max_len);
	}
	nents = vmsg_sglist_nents(pmsg);
	if(nents > XIO_IOVLEN){
		ARPC_LOG_ERROR("data depth over limit, nents:%u, max:%u.", 
						nents, 
						XIO_IOVLEN);
		return -1;
	}
	return 0;
}

int keep_conn_heartbeat(const struct arpc_connection *conn)
{
	int ret;
	CONN_CTX(ctx, conn, ARPC_ERROR);
	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_TELL_LIVE);
	arpc_cond_unlock(&ctx->cond);
	return 0;
}

static void check_session_active(struct arpc_connection_ctx *ctx)
{
	int ret;
	QUEUE* iter;
	struct arpc_connection *conn;
	struct arpc_session_handle *session;
	struct arpc_connection_ctx *conn_ctx;
	int is_active = 0;

	if (ctx->conn_timeout_ms <= 0 || ctx->type != ARPC_CON_TYPE_CLIENT) {
		return;
	}
	if (!ctx->session) {
		return;
	}
	session = ctx->session;
	ret = arpc_cond_lock(&session->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ;, "arpc_cond_lock session[%p] fail.", session);

	QUEUE_FOREACH_VAL(&session->q_con, iter,
	{
		conn = QUEUE_DATA(iter, struct arpc_connection, q);
		conn_ctx = (struct arpc_connection_ctx *)conn->ctx;
		ret = arpc_cond_lock(&conn_ctx->cond);
		if(IS_SET(conn_ctx->flags, ARPC_CONN_ATTR_TELL_LIVE)) {
			is_active = 1;
			arpc_cond_unlock(&conn_ctx->cond);
			break;
		}
		arpc_cond_unlock(&conn_ctx->cond);
	});
	arpc_cond_unlock(&session->cond);

	arpc_cond_lock(&ctx->cond);
	if (is_active){
		CLR_FLAG(ctx->flags, ARPC_CONN_ATTR_TELL_LIVE);
	}else{
		SET_FLAG(ctx->flags, ARPC_CONN_ATTR_EXIT);
	}
	arpc_cond_unlock(&ctx->cond);
	
	return;
}


int arpc_connection_async_send(const struct arpc_connection *conn, struct arpc_common_msg  *msg)
{
	int ret;
	eventfd_t event_val = 0;
	CONN_CTX(ctx, conn, ARPC_ERROR);
	
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "arpc_conn_ow_msg null.");

	ARPC_LOG_TRACE("arpc commit tx msg, msg type:%d", msg->type);

	gettimeofday(&msg->now, NULL);	// 线程安全

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);

	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ctx->status != ARPC_CON_STA_RUN_ACTIVE), unlock, 
									"conn[%u] status[%d] not active", conn->id, ctx->status);
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_TELL_LIVE);
	ret = check_xio_msg_valid(conn, &msg->tx_msg->out);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "check msg invalid.");

	while((ctx->tx_msg_num > ARPC_CONN_TX_MAX_DEPTH) && (ctx->status == ARPC_CON_STA_RUN_ACTIVE)){
		ARPC_LOG_NOTICE("con is busy, tx_msg_num[%lu] wait release,", ctx->tx_msg_num);
		ret = arpc_cond_wait_timeout(&ctx->cond, 1 + 2*ctx->tx_msg_num);
		LOG_ERROR_IF_VAL_TRUE(ret, "conn[%u][%p], xio con[%p] wait timeout to coninue.", conn->id, conn, ctx->xio_con);
		if (ret){
			continue;
		}
		ret = 0;
		break;
	}
	arpc_cond_unlock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "connoection invalid, send msg[%d] fail.", msg->type);
	
	assert(msg->status == ARPC_MSG_STATUS_USED);
	arpc_mutex_lock(&ctx->msg_lock);
	QUEUE_REMOVE(&msg->q);
	QUEUE_INIT(&msg->q);
	msg->status = ARPC_MSG_STATUS_TX;
	QUEUE_INSERT_TAIL(&ctx->q_tx_msg, &msg->q);
	ctx->tx_msg_num++;
	ctx->event_cnt++;
	if (ctx->event_cnt > 500000) {
		ctx->event_cnt = 0;
		(void)eventfd_read(ctx->event_fd, &event_val);
	}
	arpc_mutex_unlock(&ctx->msg_lock);

	ret = eventfd_write(ctx->event_fd, ARPC_CONN_EVENT_E_SEND_DATA);//触发发送事件
	LOG_ERROR_IF_VAL_TRUE(ret < 0, "ret[%d], write fd[%d] fail", ret, ctx->event_fd);
	//sched_yield();//CPU让出来

	if (conn->tx_interval.tv_sec + STATISTICS_PRINT_INTERVAL_S <= msg->now.tv_sec) {
		((struct arpc_connection*)conn)->tx_interval = msg->now;
		ARPC_LOG_NOTICE("### send status ###\n  # session[%p],type[%d],conid[%u],\n  # tx req cnt:%lu|ave:%lu.%06ld s,\n  # tx rsp cnt:%lu|ave:%lu.%06ld s,\n  # tx ow cnt:%lu|ave:%lu.%06ld s.\n  # wait tx cnt:%lu.\n  # busy msg cnt:%u.\n ######\n", 
						ctx->session,
						ctx->type,
						conn->id, 
						conn->tx_req_count, conn->tx_req.ave.tv_sec, conn->tx_req.ave.tv_usec,
						conn->tx_rsp_count, conn->tx_rsp.ave.tv_sec, conn->tx_rsp.ave.tv_usec,
						conn->tx_ow_count, conn->tx_ow.ave.tv_sec, conn->tx_ow.ave.tv_usec,
						ctx->tx_msg_num,
						ctx->busy_msg);
	}

	return ret;
unlock:
	arpc_cond_unlock(&ctx->cond);
	return ARPC_ERROR;
}

int arpc_check_connection_valid(struct arpc_connection *conn, enum  arpc_msg_type msg_type)
{
	int ret = 0;
	CONN_CTX(ctx, conn, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);

	if ((ctx->io_type == ARPC_IO_TYPE_IN) && (msg_type == ARPC_MSG_TYPE_OW)) {
		ARPC_LOG_DEBUG("conn[%u], io_type[%d], msg_type[%d]", conn->id, ctx->io_type, msg_type);
		arpc_cond_unlock(&ctx->cond);
		return ARPC_ERROR;
	}

	if ((ctx->tx_msg_num > ARPC_CONN_TX_MAX_DEPTH) || (ctx->status != ARPC_CON_STA_RUN_ACTIVE)) {
		ARPC_LOG_ERROR("conn[%u], tx num[%lu], status[%d], io_type[%d]", conn->id, ctx->tx_msg_num, ctx->status, ctx->io_type);
		ret = ARPC_ERROR;
	}
	arpc_cond_unlock(&ctx->cond);
	return ret;
}

int set_connection_io_type(struct arpc_connection *conn, enum arpc_io_type type)
{
	int ret = 0;
	CONN_CTX(ctx, conn, ARPC_ERROR);
	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);
	ctx->io_type = type;
	arpc_cond_unlock(&ctx->cond);
	return 0;
}

int arpc_connection_send_comp_notify(const struct arpc_connection *conn, struct arpc_common_msg *msg)
{
	int ret;
	CONN_CTX(ctx, conn, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);
	arpc_cond_notify(&ctx->cond);
	arpc_cond_unlock(&ctx->cond);

	return 0;
}

struct arpc_session_ops *arpc_get_ops(struct arpc_connection *con)
{
	CONN_CTX(ctx, con, NULL);
	return ctx->session?(&ctx->session->ops):NULL;
}
void *arpc_get_ops_ctx(struct arpc_connection *con)
{
	CONN_CTX(ctx, con, NULL);
	return ctx->session?(ctx->session->usr_context):NULL;
}
uint32_t arpc_get_max_iov_len(struct arpc_connection *con)
{
	CONN_CTX(ctx, con, IOV_DEFAULT_MAX_LEN);
	return ctx->msg_iov_max_len >8 ? ctx->msg_iov_max_len: IOV_DEFAULT_MAX_LEN;
}

struct arpc_session_handle *arpc_get_conn_session(struct arpc_connection *con)
{
	CONN_CTX(ctx, con, NULL);
	return ctx->session;
}

enum arpc_connection_type arpc_get_conn_type(struct arpc_connection *con)
{
	CONN_CTX(ctx, con, ARPC_CON_TYPE_NONE);
	return ctx->type;
}

static inline int alloc_common_msg(QUEUE* iter, uint32_t size, enum  arpc_msg_type type)
{
	int i;
	struct arpc_common_msg *req_msg = NULL;
	if(QUEUE_EMPTY(iter)) {
		for (i = 0; i < COMM_MSG_GROW_STEP; i++) {
			req_msg = arpc_create_common_msg(size);
			LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, -1, "req_msg arpc_create_common_msg fail.");
			req_msg->type = type;
			QUEUE_INIT(&req_msg->q);
			QUEUE_INSERT_TAIL(iter, &req_msg->q);
		}
	}
	return 0;
}

struct arpc_common_msg *get_common_msg(const struct arpc_connection *conn, enum  arpc_msg_type type)
{
	int ret;
	QUEUE* iter = NULL;
	int i;
	struct arpc_common_msg *req_msg = NULL;
	CONN_CTX(ctx, conn, NULL);

	arpc_mutex_lock(&ctx->msg_lock);
	switch (type)
	{
	case ARPC_MSG_TYPE_REQ:
		ret = alloc_common_msg(&ctx->q_req_msg, sizeof(struct arpc_request_handle), type);
		if (ret) {
			ARPC_LOG_ERROR("alloc_common_msg for req fail.");
			break;
		}
		iter = QUEUE_HEAD(&ctx->q_req_msg);
		break;
	case ARPC_MSG_TYPE_RSP:
		ret = alloc_common_msg(&ctx->q_rsp_msg, sizeof(struct arpc_rsp_handle), type);
		if (ret) {
			ARPC_LOG_ERROR("alloc_common_msg for rsp fail.");
			break;
		}
		iter = QUEUE_HEAD(&ctx->q_rsp_msg);
		break;
	case ARPC_MSG_TYPE_OW:
		ret = alloc_common_msg(&ctx->q_ow_msg, sizeof(struct arpc_oneway_handle), type);
		if (ret) {
			ARPC_LOG_ERROR("alloc_common_msg for ow fail.");
			break;
		}
		iter = QUEUE_HEAD(&ctx->q_ow_msg);
		break;
	default:
		ret = -1;
		break;
	}
	if (!ret){
		QUEUE_REMOVE(iter);
		QUEUE_INIT(iter);
		req_msg = QUEUE_DATA(iter, struct arpc_common_msg, q);
		req_msg->status = ARPC_MSG_STATUS_USED;
		req_msg->conn = conn;
		QUEUE_INSERT_TAIL(&ctx->q_used_msg, &req_msg->q);
		ctx->busy_msg++;
	}
	arpc_mutex_unlock(&ctx->msg_lock);
	if (req_msg) {
		memset(&req_msg->xio_msg, 0, sizeof(struct xio_msg));
	}
	
	return req_msg;
}

void put_common_msg(struct arpc_common_msg *msg)
{
	struct arpc_connection *conn;
	struct arpc_connection_ctx *ctx;
	int ret;

	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ;, "msg  is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg->conn, ;, "msg  is null.");
	ret = arpc_cond_lock(&msg->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ;, "arpc_cond_lock msg fail.");
	if (msg->status == ARPC_MSG_STATUS_IDLE) {
		arpc_cond_unlock(&msg->cond);
		return;
	}
	assert(msg->status == ARPC_MSG_STATUS_USED);
	msg->status = ARPC_MSG_STATUS_IDLE;
	msg->retry_cnt = 0;
	msg->flag = 0;
	msg->xio_msg.flags = 0;
	arpc_cond_unlock(&msg->cond);
	conn = (struct arpc_connection *)msg->conn;
	ctx = (struct arpc_connection_ctx *)(conn->ctx);
	arpc_mutex_lock(&ctx->msg_lock);
	QUEUE_REMOVE(&msg->q);
	QUEUE_INIT(&msg->q);
	switch (msg->type)
	{
	case ARPC_MSG_TYPE_REQ:
		statistics_per_time(&msg->now, &conn->tx_req, 3);
		QUEUE_INSERT_TAIL(&ctx->q_req_msg, &msg->q);
		break;
	case ARPC_MSG_TYPE_RSP:
		statistics_per_time(&msg->now, &conn->tx_rsp, 3);
		QUEUE_INSERT_TAIL(&ctx->q_rsp_msg, &msg->q);
		break;
	case ARPC_MSG_TYPE_OW:
		statistics_per_time(&msg->now, &conn->tx_ow, 3);
		QUEUE_INSERT_TAIL(&ctx->q_ow_msg, &msg->q);
		break;
	default:
		ARPC_LOG_ERROR("unkown type");
		break;
	}
	ctx->busy_msg--;
	arpc_mutex_unlock(&ctx->msg_lock);
	return;
}