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

#include "arpc_connection.h"

#define WAIT_THREAD_RUNING_TIMEOUT (1000)

static const char pipe_magic[] = "msg";

#define ARPC_EVENT_BUF_MAX_LEN 	4
#define ARPC_EVENT_DISCONNECT 	"1"
#define ARPC_EVENT_SEND_DATA 	"2"

#define ARPC_CONN_TX_MAX_DEPTH	  		50
#define ARPC_MAX_TX_CNT_PER_WAKEUP		10

#define ARPC_CONN_EXIT_MAX_TIMES_MS	(2*1000)

#define ARPC_CONN_ATTR_TELL_LIVE  (1<<0)
#define ARPC_CONN_ATTR_ADD_EVENT  (1<<1)
#define ARPC_CONN_ATTR_REBUILD    (1<<2)
#define ARPC_CONN_ATTR_EXIT       (1<<3)

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
	enum arpc_connection_type   type;				
	enum arpc_connection_status	status;
	void						*usr_ctx;
	uint32_t					flags;
	uint8_t						is_busy;
	enum arpc_connection_mode	conn_mode;
	struct arpc_cond 			cond;
	uint64_t					tx_msg_num;
	QUEUE     					q_tx_msg;
	int 						pipe_fd[2];	
	uint64_t					tx_end_num;
	QUEUE     					q_tx_end;
	int64_t						conn_timeout_ms;					
};

static int arpc_client_run_loop(void * thread_ctx);
static int  arpc_add_event_to_conn(struct arpc_connection *con);
static int  arpc_del_event_to_conn(struct arpc_connection *con);
static void arpc_tx_event_callback(struct arpc_connection *usr_conn);

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
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_buf, "arpc_mutex_init fail.");
	ret = pipe(ctx->pipe_fd);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, free_cond, "pipe init fail.");
	QUEUE_INIT(&ctx->q_tx_msg);
	QUEUE_INIT(&ctx->q_tx_end);
	ctx->magic = ARPC_CONN_MAGIC;
	ctx->usr_ctx = param->usr_ctx;
	ctx->conn_timeout_ms = param->timeout_ms;
	flags = fcntl(ctx->pipe_fd[0],F_GETFL);
	flags |= O_NONBLOCK;
	ret = fcntl(ctx->pipe_fd[0], F_SETFL, flags);
	ctx->status = ARPC_CON_STA_INIT;
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
	CONN_CTX(ctx, con, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock fail, maybe free already.");
	ARPC_LOG_DEBUG("connection[%u] status:%d, type:%d",con->id, ctx->status, ctx->type);
	switch (ctx->type)
	{
		case ARPC_CON_TYPE_CLIENT:
			if (ctx->status != ARPC_CON_STA_EXIT) {
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
	
	if(ctx->pipe_fd[0] > 0){
		close(ctx->pipe_fd[0]);
		ctx->pipe_fd[0] = -1;
	}
	if(ctx->pipe_fd[1] > 0){
		close(ctx->pipe_fd[1]);
		ctx->pipe_fd[1] = -1;
	}
	ctx->magic = 0;
	arpc_cond_unlock(&ctx->cond);
	ret = arpc_cond_destroy(&ctx->cond); 
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_destroy fail.");
	SAFE_FREE_MEM(con);
	return 0;
unlock:
	arpc_cond_unlock(&ctx->cond);
	return ARPC_ERROR;
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
		ret = write(ctx->pipe_fd[1], ARPC_EVENT_DISCONNECT, sizeof(ARPC_EVENT_DISCONNECT));//触发发送事件
		LOG_ERROR_IF_VAL_TRUE(ret < 0, "ret[%d], write fd[%d] fail", ret, ctx->pipe_fd[1]);
	}
	arpc_cond_unlock(&ctx->cond);

	return 0;
}

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
	(void)memset(&xio_con_param, 0, sizeof(struct xio_connection_params));
	xio_con_param.session			= ctx->session->xio_s;
	xio_con_param.ctx				= ctx->xio_con_ctx;
	xio_con_param.conn_idx			= con->id;
	xio_con_param.conn_user_context	= con;
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_REBUILD);
	ctx->xio_con = xio_connect(&xio_con_param);
	LOG_ERROR_IF_VAL_TRUE(!ctx->xio_con, "xio_connect fail.");
	xio_context_stop_loop(ctx->xio_con_ctx);
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
	cpu_set_t		cpuset;
	int ret;
	char thread_name[16+1] ={0};
	CONN_CTX(ctx, con, ARPC_ERROR);

	LOG_THEN_RETURN_VAL_IF_TRUE(ctx->magic != ARPC_CONN_MAGIC, -1, "magic error.");

	ARPC_LOG_DEBUG("connection run on the thread[%lu].", pthread_self());

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock con cond fail.");
	ctx->status = ARPC_CON_STA_RUN;
	ret = arpc_connect_init(con);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, exit_thread, "arpc_connect_init fail.");

	CPU_ZERO(&cpuset);
	CPU_SET((con->id + 1)%(arpc_cpu_max_num()), &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	snprintf(thread_name, sizeof(thread_name), "arpc_conn_%d", con->id);
	prctl(PR_SET_NAME, thread_name);

	arpc_cond_notify(&ctx->cond);
	ARPC_LOG_DEBUG("conn_timeout_ms[%ld] timeout.", ctx->conn_timeout_ms);
	if(ctx->conn_timeout_ms <= 0) {
		ctx->conn_timeout_ms = XIO_INFINITE;
	}
	for(;;){
		arpc_cond_unlock(&ctx->cond);
		ret = xio_context_run_loop(ctx->xio_con_ctx, ctx->conn_timeout_ms);
		if (ret) {
			ARPC_LOG_ERROR("xio loop error msg: %s.", xio_strerror(xio_errno()));
		}
		ret = arpc_cond_lock(&ctx->cond);
		LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock con cond fail.");

		if (IS_SET(ctx->flags, ARPC_CONN_ATTR_EXIT)) {
			CLR_FLAG(ctx->flags, ARPC_CONN_ATTR_EXIT);
			break;
		}

		if (IS_SET(ctx->flags, ARPC_CONN_ATTR_REBUILD)) {
			CLR_FLAG(ctx->flags, ARPC_CONN_ATTR_REBUILD);
			continue;
		}

		if (IS_SET(ctx->flags, ARPC_CONN_ATTR_TELL_LIVE)) {
			CLR_FLAG(ctx->flags, ARPC_CONN_ATTR_TELL_LIVE);
			continue;
		}

		ARPC_LOG_DEBUG("xio connection[%u] timeout to disconnect.", con->id);
		if (ctx->status == ARPC_CON_STA_RUN_ACTIVE) {
			xio_disconnect(ctx->xio_con);
			continue;
		}
		break;
	}

exit_thread:
	ctx->status = ARPC_CON_STA_EXIT;
	if (ctx->xio_con_ctx) {
		xio_context_destroy(ctx->xio_con_ctx);
		ctx->xio_con_ctx = NULL;
	}
	
	prctl(PR_SET_NAME, "share_thread");
	arpc_cond_notify(&ctx->cond);
	ARPC_LOG_DEBUG("xio connection[%u] on thread[%lu] exit now.", con->id,  pthread_self());
	arpc_cond_unlock(&ctx->cond);
	return ARPC_SUCCESS;
}

static void arpc_conn_event_callback(int fd, int events, void *data)
{
	struct arpc_connection *con = (struct arpc_connection *)data;
	int ret;
	int type = 0;
	char buf[ARPC_EVENT_BUF_MAX_LEN+1] = {0};
	CONN_CTX(ctx, con, ;);
	
	ret = read(fd, buf, sizeof(pipe_magic));//避免阻塞
	LOG_ERROR_IF_VAL_TRUE(ret < 0, "read fd[%d] fail, ret[%d]", fd, ret);
	//type = atoi(buf);
	//ARPC_LOG_NOTICE("type[%d]", type);
	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ;, "tx event  arpc_cond_lock fail.");
	switch (ctx->status)
	{
	case ARPC_CON_STA_CLEANUP:
		ARPC_LOG_DEBUG("conn status[%d] to disconnect", ctx->status);
		if (ctx->xio_con) {
			ret = xio_disconnect(ctx->xio_con);
			if (ret) {
				ARPC_LOG_ERROR("xio disconnect error msg: %s.", xio_strerror(xio_errno()));
			}
		}
		break;
	case ARPC_CON_STA_RUN_ACTIVE:
		arpc_tx_event_callback(con);//发送事件
		arpc_cond_notify(&ctx->cond);//唤醒多个线程
		break;
	default:
		ARPC_LOG_ERROR("conn status[%d] unkown", ctx->status);
		break;
	}
	
	arpc_cond_unlock(&ctx->cond);
	return;
}
static void arpc_tx_event_callback(struct arpc_connection *usr_conn)
{
	int ret;
	QUEUE* iter;
	struct arpc_common_msg *msg;
	int max_tx_send = ARPC_MAX_TX_CNT_PER_WAKEUP; //每次唤醒最多发送消息数，避免阻塞太长
	CONN_CTX(con, usr_conn, ;);

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
			continue;
		}
		msg->retry_cnt = 0;
		QUEUE_INSERT_TAIL(&con->q_tx_end, iter);//移到已发送的队列
		con->tx_msg_num--;
		con->tx_end_num++;
		max_tx_send--;
	}
	
	return;
}

static int  arpc_add_event_to_conn(struct arpc_connection *con)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);
	LOG_THEN_RETURN_VAL_IF_TRUE(!ctx->xio_con_ctx, ARPC_ERROR, "xio_con_ctx is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(ctx->pipe_fd[0] < 0, ARPC_ERROR, "pipe_fd[1] is invalid.");
	//注册写事件
	ret = xio_context_add_ev_handler(ctx->xio_con_ctx, ctx->pipe_fd[0], XIO_POLLIN|XIO_POLLET, arpc_conn_event_callback, con);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "xio_context_add_ev_handler fail.");
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_ADD_EVENT);
	return 0;
}

static int  arpc_del_event_to_conn(struct arpc_connection *con)
{
	int ret;
	CONN_CTX(ctx, con, ARPC_ERROR);
	LOG_THEN_RETURN_VAL_IF_TRUE(!ctx->xio_con_ctx, ARPC_ERROR, "xio_con_ctx is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(ctx->pipe_fd[0] < 0, ARPC_ERROR, "pipe_fd[1] is invalid.");
	if (IS_SET(ctx->flags, ARPC_CONN_ATTR_ADD_EVENT)){
		ret = xio_context_del_ev_handler(ctx->xio_con_ctx, ctx->pipe_fd[0]);
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
	if(nents > (ctx->msg_data_max_len/ctx->msg_iov_max_len)){
		ARPC_LOG_ERROR("data depth over limit, nents:%u, max:%lu.", 
						nents, 
						(ctx->msg_data_max_len/ctx->msg_iov_max_len));
		return -1;
	}
	return 0;
}

static int connection_async_send(struct arpc_connection *conn, struct arpc_common_msg  *msg)
{
	int ret;
	CONN_CTX(ctx, conn, ARPC_ERROR);

	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "arpc_conn_ow_msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((ctx->status != ARPC_CON_STA_RUN_ACTIVE), ARPC_ERROR, "conn[%u] status[%d] not active", conn->id, ctx->status);

	ret = check_xio_msg_valid(conn, &msg->tx_msg->out);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "check msg invalid.");

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
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "connoection invalid, send msg[%d] fail.", msg->type);

	ctx->tx_msg_num++;
	QUEUE_INIT(&msg->q);
	QUEUE_INSERT_TAIL(&ctx->q_tx_msg, &msg->q);

	msg->ref++;//引用计数
	ret = write(ctx->pipe_fd[1], ARPC_EVENT_SEND_DATA, sizeof(ARPC_EVENT_SEND_DATA));//触发发送事件
	LOG_ERROR_IF_VAL_TRUE(ret < 0, "ret[%d], write fd[%d] fail", ret, ctx->pipe_fd[1]);

	return 0;
}

int arpc_connection_async_send(struct arpc_connection *conn, struct arpc_common_msg  *msg)
{
	int ret;
	CONN_CTX(ctx, conn, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);
	SET_FLAG(ctx->flags, ARPC_CONN_ATTR_TELL_LIVE);
	ret = connection_async_send(conn, msg);
	arpc_cond_unlock(&ctx->cond);
	return ret;
}

int arpc_check_connection_valid(struct arpc_connection *conn)
{
	int ret;
	CONN_CTX(ctx, conn, ARPC_ERROR);

	ret = arpc_cond_trylock(&ctx->cond);
	if (ret) {
		return ret;
	}
	if ((ctx->tx_msg_num > ARPC_CONN_TX_MAX_DEPTH) || (ctx->status != ARPC_CON_STA_RUN_ACTIVE)) {
		ret = ARPC_ERROR;
	}
	arpc_cond_unlock(&ctx->cond);
	return ret;
}


int arpc_connection_send_comp_notify(struct arpc_connection *conn, struct arpc_common_msg *msg)
{
	int ret;
	CONN_CTX(ctx, conn, ARPC_ERROR);

	ret = arpc_cond_lock(&ctx->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock conn[%u][%p] fail.", conn->id, conn);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE((ctx->status != ARPC_CON_STA_RUN_ACTIVE), unlock, "connoection not active");

	ret = arpc_cond_lock(&msg->cond);
	if(!ret){
		QUEUE_REMOVE(&msg->q);
		QUEUE_INIT(&msg->q);
		msg->ref--;
		arpc_cond_unlock(&msg->cond);
	}else{
		ARPC_LOG_ERROR(" lock msg fail, maybe free...");
	}
	if(ctx->tx_end_num){
		ctx->tx_end_num--;
	}
	arpc_cond_notify(&ctx->cond);
	arpc_cond_unlock(&ctx->cond);
	return 0;
unlock:
	arpc_cond_unlock(&ctx->cond);
	return -1;
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