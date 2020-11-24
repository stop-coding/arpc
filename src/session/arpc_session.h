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

#ifndef _ARPC_SESSION_H
#define _ARPC_SESSION_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>

#include "base_log.h"
#include "queue.h"
#include "arpc_com.h"
#include "arpc_message.h"
#include "arpc_connection.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SESSION_CTX(session_fd, session_usr_ctx)	\
struct arpc_session_handle *session_fd = (struct arpc_session_handle *)session_usr_ctx;

#define ARPC_SESSION_ATTR_AUTO_DISCONNECT  (1<<15)

enum arpc_session_type{
	ARPC_SESSION_CLIENT = 0, //
	ARPC_SESSION_SERVER, 	//
	ARPC_SESSION_NONE, 	//
};

struct arpc_client_ctx {
	struct xio_session_params	xio_param;
	char 						uri[URI_MAX_LEN];
	void						*private_data;  /**< private user data snt to */
	uint64_t					private_data_len; /**< private data length    */
};

struct arpc_session_handle{
	QUEUE     q;
	QUEUE     q_con;
	struct arpc_mutex  lock;
	uint32_t 	conn_num;
	uint32_t 	reconnect_times;
	uint64_t 	tx_total;
	void 		*threadpool;
	enum arpc_session_type type;
	struct xio_session *xio_s;
	uint32_t 	is_close;
	int32_t		flags;
	enum arpc_session_status status;
	struct arpc_cond  cond;
	struct arpc_session_ops	  ops;
	uint32_t	msg_head_max_len;
	uint64_t	msg_data_max_len;
	uint32_t	msg_iov_max_len;
	int32_t		conn_timeout_ms;
	void 	*usr_context;			// 用户上下文
	char    ex_ctx[0];			/* exterd handle */
};

struct arpc_session_handle *arpc_create_session(enum arpc_session_type type, uint32_t ex_ctx_size);
int arpc_destroy_session(struct arpc_session_handle* session, int64_t timeout_ms);

int arpc_session_connect_for_client(struct arpc_session_handle *session, int64_t timeout_ms);
int arpc_session_disconnect_for_client(struct arpc_session_handle *session, int64_t timeout_ms);

int session_client_teardown_event(struct arpc_session_handle *session);
int session_established_for_client(struct arpc_session_handle *session);
int session_notify_wakeup(struct arpc_session_handle *session);

int session_insert_con(struct arpc_session_handle *s, struct arpc_connection *con);
int session_remove_con(struct arpc_session_handle *s, struct arpc_connection *con);
int session_move_con_tail(struct arpc_session_handle *s, struct arpc_connection *con);

int session_get_idle_conn(struct arpc_session_handle *session, struct arpc_connection **conn, 
							enum  arpc_msg_type msg_type, int64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */
