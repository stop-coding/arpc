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

#ifndef _ARPC_CONNECTION_H
#define _ARPC_CONNECTION_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>

#include "base_log.h"
#include "queue.h"
#include "arpc_com.h"
#include "arpc_message.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARPC_CONN_CTX(conn, con_usr_ctx)	\
struct arpc_connection *conn = (struct arpc_connection *)con_usr_ctx;

#define ARPC_CONN_OPS_CTX(conn, conn_ops, con_usr_ctx)	\
struct arpc_connection *conn = (struct arpc_connection *)con_usr_ctx;\
struct arpc_session_ops	*conn_ops;\
do{if(conn){conn_ops = arpc_get_ops(conn);}else{ARPC_LOG_ERROR("conn ops is null");}}while(0);

enum arpc_connection_type{
	ARPC_CON_TYPE_NONE = 0,
	ARPC_CON_TYPE_CLIENT, //
	ARPC_CON_TYPE_SERVER, 	//
};

#define ARPC_CONN_MAGIC 0xff36a97
struct arpc_connection {
	QUEUE 						q;
	uint32_t					id;
	char						ctx[0];
};

struct arpc_session_handle;
struct arpc_session_ops;

struct arpc_connection_param {
	enum arpc_connection_type type;
	uint32_t 				id;
	struct arpc_session_handle  *session;
	struct xio_connection		*xio_con;
	struct xio_context			*xio_con_ctx;
	int64_t					timeout_ms;
	void					*usr_ctx;
};

struct arpc_connection *arpc_create_connection(const struct arpc_connection_param *param);
int arpc_destroy_connection(struct arpc_connection *con);

int arpc_client_connect(struct arpc_connection *con, int64_t timeout_s);
int arpc_client_wait_connected(struct arpc_connection *con, int64_t timeout_ms);
int arpc_client_disconnect(struct arpc_connection *con, int64_t timeout_s);

int arpc_client_conn_stop_loop_r(struct arpc_connection *con);
int arpc_client_conn_stop_loop(struct arpc_connection *con);
int arpc_client_reconnect(struct arpc_connection *con);

int arpc_set_connect_status(struct arpc_connection *con);
int arpc_set_disconnect_status(struct arpc_connection *con);

struct arpc_session_ops *arpc_get_ops(struct arpc_connection *con);
void *arpc_get_ops_ctx(struct arpc_connection *con);
uint32_t arpc_get_max_iov_len(struct arpc_connection *con);

int arpc_lock_connection(struct arpc_connection *con);
int arpc_unlock_connection(struct arpc_connection *con);

int arpc_check_connection_valid(struct arpc_connection *conn);

int arpc_connection_async_send(const struct arpc_connection *conn, struct arpc_common_msg  *msg);
int arpc_connection_send_comp_notify(const struct arpc_connection *conn, struct arpc_common_msg *msg);

int check_xio_msg_valid(const struct arpc_connection *conn, const struct xio_vmsg *pmsg);

struct arpc_common_msg *get_common_msg(const struct arpc_connection *conn, enum  arpc_msg_type type);
void put_common_msg(struct arpc_common_msg *msg);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */
