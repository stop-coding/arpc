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

struct arpc_session_handle *arpc_create_session(enum arpc_session_type type, uint32_t ex_ctx_size)
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
	session->status = ARPC_SES_STA_INIT;
	session->conn_timeout_ms = XIO_INFINITE;
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
	session->is_close = 1;
	arpc_cond_notify_all(&session->cond);//通知释放资源
	arpc_cond_unlock(&session->cond);

	ret = arpc_cond_lock(&session->cond); /* 锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session fail.");

	// 断开链路
	if (session->type == ARPC_SESSION_CLIENT && session->status == ARPC_SES_STA_ACTIVE) {
		QUEUE_FOREACH_VAL(&session->q_con, iter, 
		{
			con = QUEUE_DATA(iter, struct arpc_connection, q);
			ret = arpc_client_disconnect(con, timeout_ms);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_client_disconnect fail.");
		});
		ARPC_LOG_DEBUG(" to wait session close, status[%d]......", session->status);
		if(timeout_ms && session->status != ARPC_SES_STA_CLOSE){
			ret = arpc_cond_wait_timeout(&session->cond, timeout_ms);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_wait_timeout session status [%d] timeout.", session->status);
		}
	}
	
	// 释放con资源
	while(!QUEUE_EMPTY(&session->q_con)){
		iter = QUEUE_HEAD(&session->q_con);
		con = QUEUE_DATA(iter, struct arpc_connection, q);

		arpc_lock_connection(con);
		QUEUE_REMOVE(iter);
		QUEUE_INIT(iter);
		arpc_unlock_connection(con);

		ret = arpc_destroy_connection(con);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_destroy_connection fail.");
	}
	// 
	arpc_cond_unlock(&session->cond);

	ret = arpc_cond_destroy(&session->cond);
	LOG_ERROR_IF_VAL_TRUE(ret, "arpc_cond_destroy fail.");

	SAFE_FREE_MEM(session);
	return 0;
}

int arpc_wait_session_active(struct arpc_session_handle *session, int64_t timeout_ms)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!session, ARPC_ERROR, "pcon is null.");
	ret = arpc_cond_lock(&session->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_mutex_lock session[%p] fail.", session);

	if(session->status == ARPC_SES_STA_CLOSE){
		arpc_cond_unlock(&session->cond);
		return ARPC_ERROR;
	}
	if(session->status == ARPC_SES_STA_ACTIVE){
		arpc_cond_unlock(&session->cond);
		return 0;
	}
	ret = arpc_cond_wait_timeout(&session->cond, timeout_ms);
	if(ret || session->status != ARPC_SES_STA_ACTIVE){
		ARPC_LOG_ERROR("session[%p] connection fail, status[%d].", session, session->status);
		arpc_cond_unlock(&session->cond);
		return ARPC_ERROR;
	}
	arpc_cond_unlock(&session->cond);
	return 0;
}

int arpc_session_connect_for_client(struct arpc_session_handle *session, int64_t timeout_ms)
{
	int ret = 0;
	QUEUE* iter;
	struct arpc_connection *con;

	LOG_THEN_RETURN_VAL_IF_TRUE(!session, -1, "session null.");
	ret = arpc_cond_lock(&session->cond); 
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session fail.");
	if (session->type != ARPC_SESSION_CLIENT || session->status == ARPC_SES_STA_ACTIVE) {
		goto success;
	}
	QUEUE_FOREACH_VAL(&session->q_con, iter, 
	{
		con = QUEUE_DATA(iter, struct arpc_connection, q);
		ret = arpc_client_connect(con, timeout_ms);
		LOG_ERROR_IF_VAL_TRUE(ret, " client conn[%u] connect fail.", con->id);
	});

	if(session->status != ARPC_SES_STA_ACTIVE){
		ret = arpc_cond_wait_timeout(&session->cond, timeout_ms);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "wait session[%d] connect timeout.", session->status);
	}

	QUEUE_FOREACH_VAL(&session->q_con, iter, 
	{
		con = QUEUE_DATA(iter, struct arpc_connection, q);
		ret = arpc_client_wait_connected(con, timeout_ms);
		LOG_ERROR_IF_VAL_TRUE(ret, " client wait conn[%u] connected fail.", con->id);
	});
success:
	arpc_cond_unlock(&session->cond);

	return 0;
unlock:
	QUEUE_FOREACH_VAL(&session->q_con, iter, 
	{
		con = QUEUE_DATA(iter, struct arpc_connection, q);
		ret = arpc_client_disconnect(con, timeout_ms);
		LOG_ERROR_IF_VAL_TRUE(ret, "arpc_client_disconnect fail.");
	});
	arpc_cond_unlock(&session->cond);
	return -1;
}

int arpc_session_disconnect_for_client(struct arpc_session_handle *session, int64_t timeout_ms)
{
	int ret = 0;
	QUEUE* iter;
	struct arpc_connection *con;

	LOG_THEN_RETURN_VAL_IF_TRUE(!session, -1, "session null.");
	ret = arpc_cond_lock(&session->cond); 
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session fail.");
	QUEUE_FOREACH_VAL(&session->q_con, iter, 
	{
		con = QUEUE_DATA(iter, struct arpc_connection, q);
		ret = arpc_client_disconnect(con, timeout_ms);
		LOG_ERROR_IF_VAL_TRUE(ret, "client conn[%u] disconnect fail.", con->id);
	});
	arpc_cond_unlock(&session->cond);
	return 0;
}
int session_established_for_client(struct arpc_session_handle *session)
{
	LOG_THEN_RETURN_VAL_IF_TRUE(!session, ARPC_ERROR, "session is null.");
	arpc_cond_lock(&session->cond);
	session->status = ARPC_SES_STA_ACTIVE;
	arpc_cond_notify_all(&session->cond);
	arpc_cond_unlock(&session->cond);
	ARPC_LOG_NOTICE("client established session[%p] success!!", session);
	return 0;
}

int session_notify_wakeup(struct arpc_session_handle *session)
{
	LOG_THEN_RETURN_VAL_IF_TRUE(!session, ARPC_ERROR, "session is null.");
	arpc_cond_lock(&session->cond);
	arpc_cond_notify(&session->cond);
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

	ret = arpc_cond_lock(&session->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_mutex_lock session[%p] fail.", session);
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(session->type != ARPC_SESSION_CLIENT, unlock, "session not client.");

	if (session->is_close || IS_SET(session->flags, ARPC_SESSION_ATTR_AUTO_DISCONNECT)) {
		session->status = ARPC_SES_STA_CLOSE;
		QUEUE_FOREACH_VAL(&session->q_con, iter,
		{
			con = QUEUE_DATA(iter, struct arpc_connection, q);
			ret = arpc_client_conn_stop_loop(con);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_client_conn_stop_loop fail.");
		});
		session->xio_s = NULL;
		ARPC_LOG_NOTICE("stop connection loop of session[%p] end!!", session);
	}else{
		client_ctx = (struct arpc_client_ctx *)session->ex_ctx;
		session->xio_s = xio_session_create(&client_ctx->xio_param);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!session->xio_s, unlock, "xio_session_create fail.");
		QUEUE_FOREACH_VAL(&session->q_con, iter,
		{
			con = QUEUE_DATA(iter, struct arpc_connection, q);
			ret = arpc_client_reconnect(con);
			LOG_ERROR_IF_VAL_TRUE(ret, "arpc_client_reconnect fail.");
		});
		ARPC_LOG_NOTICE("rebuild session[%p] end!!", session);
	}
	arpc_cond_notify_all(&session->cond);
	arpc_cond_unlock(&session->cond);

	return 0;
unlock:
	arpc_cond_unlock(&session->cond);
	return -1;
}

int session_insert_con(struct arpc_session_handle *s, struct arpc_connection *con)
{
	int ret;
	ret = arpc_cond_lock(&s->cond); /* 锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session[%p] fail.", s);

	arpc_lock_connection(con);
	QUEUE_INSERT_TAIL(&s->q_con, &con->q);
	arpc_unlock_connection(con);

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

	arpc_lock_connection(con);
	QUEUE_REMOVE(&con->q);
	QUEUE_INIT(&con->q);
	arpc_unlock_connection(con);

	s->conn_num--;
	arpc_cond_notify(&s->cond);
	arpc_cond_unlock(&s->cond);
	return 0;
}

int session_move_con_tail(struct arpc_session_handle *s, struct arpc_connection *con)
{
	int ret;
	ret = arpc_cond_lock(&s->cond); /* 锁 */
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, -1, "arpc_cond_lock session[%p] fail.", s);

	arpc_lock_connection(con);
	QUEUE_REMOVE(&con->q);
	QUEUE_INSERT_TAIL(&s->q_con, &con->q);
	arpc_unlock_connection(con);

	arpc_cond_unlock(&s->cond);
	return 0;
}

int session_async_send(struct arpc_session_handle *session, struct arpc_common_msg  *msg, int64_t timeout_ms)
{
	int ret;
	QUEUE* iter;
	struct arpc_client_ctx *client_ctx;
	struct arpc_connection *con = NULL;

	LOG_THEN_RETURN_VAL_IF_TRUE(!session, ARPC_ERROR, "session is null.");

	ret = arpc_cond_lock(&session->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_mutex_lock session[%p] fail.", session);
	if (session->type == ARPC_SESSION_CLIENT && session->status == ARPC_SES_STA_CLOSE) {

		client_ctx = (struct arpc_client_ctx *)session->ex_ctx;
		session->xio_s = xio_session_create(&client_ctx->xio_param);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE(!session->xio_s, unlock, "xio_session_create fail.");

		QUEUE_FOREACH_VAL(&session->q_con, iter, 
		{
			con = QUEUE_DATA(iter, struct arpc_connection, q);
			ret = arpc_client_connect(con, timeout_ms);
			LOG_ERROR_IF_VAL_TRUE(ret, " client conn[%u] connect fail.", con->id);
		});

		if(session->status != ARPC_SES_STA_ACTIVE){
			ret = arpc_cond_wait_timeout(&session->cond, timeout_ms);
			LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, unlock, "wait session[%d] connect timeout.", session->status);
		}

		QUEUE_FOREACH_VAL(&session->q_con, iter, 
		{
			con = QUEUE_DATA(iter, struct arpc_connection, q);
			ret = arpc_client_wait_connected(con, timeout_ms);
			LOG_ERROR_IF_VAL_TRUE(ret, " client wait conn[%u] connected fail.", con->id);
		});
	}
	if(QUEUE_EMPTY(&session->q_con) || session->status != ARPC_SES_STA_ACTIVE){
		ARPC_LOG_ERROR("session[%p] invalid, session status[%d]", session, session->status);
		arpc_cond_unlock(&session->cond);
		return ARPC_ERROR;
	}
	while(!con){
		QUEUE_FOREACH_VAL(&session->q_con, iter,
		{
			con = QUEUE_DATA(iter, struct arpc_connection, q);
			ret = arpc_check_connection_valid(con);
			if(ret == ARPC_SUCCESS){
				//arpc_lock_connection(con);
				QUEUE_REMOVE(&con->q);
				QUEUE_INSERT_TAIL(&session->q_con, &con->q);
				//arpc_unlock_connection(con);
				break;
			}
			con = NULL;
		});
		
		if(con){break;}
		ARPC_LOG_NOTICE("warning: session[%p][con_num:%u] no idle connection, wait[%ld ms]...", session, session->conn_num, timeout_ms);
		ret = arpc_cond_wait_timeout(&session->cond, timeout_ms);
		if (ret) {
			ARPC_LOG_ERROR("session[%p] wait idle connection timeout[%ld ms].", session, timeout_ms);
			break;
		}
	}
	arpc_cond_unlock(&session->cond);
	return (con)?arpc_connection_async_send(con, msg):ARPC_ERROR;
unlock:
	arpc_cond_unlock(&session->cond);
	return ARPC_ERROR;
}