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

#define SERVER_CTX(server_fd, usr_ctx)	\
struct arpc_server_handle *server_fd = (struct arpc_server_handle *)usr_ctx;

#define SESSION_CTX(session_fd, session_usr_ctx)	\
struct arpc_session_handle *session_fd = (struct arpc_session_handle *)session_usr_ctx;

#define SESSION_CONN_CTX(conn, con_usr_ctx)	\
struct arpc_connection *conn = (struct arpc_connection *)con_usr_ctx;

#define SESSION_CONN_OPS_CTX(conn, conn_ops, con_usr_ctx)	\
struct arpc_connection *conn = (struct arpc_connection *)con_usr_ctx;\
struct arpc_session_ops	*conn_ops;\
do{if(conn){conn_ops = conn->ops;}else{ARPC_LOG_ERROR("conn ops is null");}}while(0);

enum session_status{
	SESSION_STA_INIT = 0, 	 //初始化完毕
	SESSION_STA_RUN, 		//运行
	SESSION_STA_RUN_ACTION, //活跃，指链路联通
	SESSION_STA_WAIT,
	SESSION_STA_CLEANUP, 	//释放
};

enum session_type{
	SESSION_CLIENT = 0, //
	SESSION_SERVER, 	//
	SESSION_SERVER_CHILD, 	//
	SESSION_NONE, 	//
};

enum xio_con_status{
	XIO_STA_INIT = 0, 	 //初始化完毕
	XIO_STA_RUN, 		//运行
	XIO_STA_RUN_ACTION, //活跃，指链路联通
	XIO_STA_TEARDOWN,
	XIO_STA_CLEANUP, 	//释放
};

enum arpc_connection_type{
	ARPC_CON_TYPE_NONE = 0,
	ARPC_CON_TYPE_CLIENT, //
	ARPC_CON_TYPE_SERVER, 	//
};

enum arpc_connection_mode{
	ARPC_CON_MODE_DIRE_IO = 0,
	ARPC_CON_MODE_DIRE_OUT, //
	ARPC_CON_MODE_DIRE_IN,  //
};

struct arpc_con_client {
	struct xio_context			*xio_con_ctx;			/* connection thread 上下文 */
	int32_t						affinity;				/* 绑定CPU */
	int32_t						recon_interval_s;
	work_handle_t 				thread_handle;
};

struct arpc_client_ctx {
	struct xio_session_params	xio_param;
	char 						uri[URI_MAX_LEN];
	void						*private_data;  /**< private user data snt to */
	uint64_t					private_data_len; /**< private data length    */
};

struct arpc_con_server {
	struct arpc_work_handle     *work;
	void						*work_ctx;
};

struct arpc_connection {
	QUEUE 						q;
	uint32_t					magic;
	uint32_t					id;
	struct xio_connection		*xio_con;				/* connection 资源*/
	enum arpc_connection_type   type;
	struct arpc_cond 			cond;				
	enum xio_con_status			status;
	struct arpc_session_handle  *session;
	struct arpc_session_ops	    *ops;
	void						*usr_ops_ctx;
	uint32_t					flags;
	uint8_t						is_busy;
	struct timeval 				access_time;
	uint32_t					aceess_cnt;
	enum arpc_connection_mode	conn_mode;
	uint64_t					tx_msg_num;
	QUEUE     					q_tx_msg;
	union
	{
		struct arpc_con_client client;
		struct arpc_con_server server;
	};
};

struct arpc_session_handle{
	QUEUE     q;
	QUEUE     q_con;
	uint32_t 	conn_num;
	uint64_t 	tx_total;
	void 		*threadpool;
	enum session_type type;
	struct xio_session *xio_s;
	uint32_t 	is_close;
	struct arpc_cond  cond;
	struct arpc_session_ops	  ops;
	void 	*usr_context;			// 用户上下文
	char    ex_ctx[0];			/* exterd handle */
};

enum xio_work_status{
	XIO_WORK_STA_INIT = 0, 	 //初始化完毕
	XIO_WORK_STA_RUN, 		//运行
	XIO_WORK_STA_EXIT, 		//运行
};


struct arpc_work_handle{
	QUEUE     	       q;
	int32_t			   affinity;				/* 绑定CPU */
	struct arpc_cond   cond;					/* 数据接收的条件变量*/
	struct xio_server	*work;
	struct xio_context	*work_ctx;			/* server thread 上下文 */
	void 	*usr_context;			// 用户上下文
	work_handle_t 		thread_handle;
	enum xio_work_status status;
	char 	uri[URI_MAX_LEN];
	char    ex_ctx[0];			/* exterd handle */
};

struct arpc_server_handle{
	QUEUE       q_work;
	QUEUE       q_session;
	void 		*threadpool;
	uint32_t 	work_num;
	uint32_t	iov_max_len;
	struct arpc_mutex  lock;
	struct arpc_session_ops	 ops;
	struct xio_server		*server;
	struct xio_context		*server_ctx;
	int (*new_session_start)(const struct arpc_new_session_req *, struct arpc_new_session_rsp *, void*);
	int (*new_session_end)(arpc_session_handle_t, struct arpc_new_session_rsp *, void*);
	int (*session_teardown)(const arpc_session_handle_t, void *usr_server_ctx, void *usr_session_ctx);
	void 	*usr_context;			// 用户上下文
	char 	uri[URI_MAX_LEN];
	uint8_t		is_stop;
	char    ex_ctx[0];			/* exterd handle */
};

struct arpc_connection *arpc_create_con(enum arpc_connection_type type, 
										struct arpc_session_handle *s, 
										uint32_t index);
int  arpc_destroy_con(struct arpc_connection *con);
int  arpc_disconnection(struct arpc_connection *con);
int  arpc_wait_connected(struct arpc_connection *con, uint64_t timeout_ms);

struct arpc_session_handle *arpc_create_session(enum session_type type, uint32_t ex_ctx_size);
int arpc_destroy_session(struct arpc_session_handle* session, int64_t timeout_ms);
int rebuild_session(struct arpc_session_handle *ses);

int session_insert_con(struct arpc_session_handle *s, struct arpc_connection *con);
int session_remove_con(struct arpc_session_handle *s, struct arpc_connection *con);
int get_connection(struct arpc_session_handle *s, struct arpc_connection **con, int64_t timeout_ms);
int set_connection_mode(struct arpc_connection *con, enum arpc_connection_mode conn_mode);

#define COMM_MSG_MAGIC 0xff8766a
int arpc_session_send_request(struct arpc_connection *conn, struct arpc_common_msg  *msg);
int arpc_session_send_oneway(struct arpc_connection *conn, struct arpc_common_msg   *msg);
int arpc_session_send_response(struct arpc_connection *conn, struct arpc_common_msg *msg);
int arpc_session_send_complete(struct arpc_connection *conn);

struct arpc_server_handle *arpc_create_server(uint32_t ex_ctx_size);
int arpc_destroy_server(struct arpc_server_handle* svr);
int server_insert_work(struct arpc_server_handle *server, struct arpc_work_handle *work);
int server_remove_work(struct arpc_server_handle *server, struct arpc_work_handle *work);
int server_insert_session(struct arpc_server_handle *server, struct arpc_session_handle *session);
int server_remove_session(struct arpc_server_handle *server, struct arpc_session_handle *session);

struct arpc_work_handle *arpc_create_xio_server_work(const struct arpc_con_info *con_param, 
													void *threadpool, 
													struct xio_session_ops *work_ops,
													uint32_t index);
int  arpc_destroy_xio_server_work(struct arpc_work_handle *work);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */
