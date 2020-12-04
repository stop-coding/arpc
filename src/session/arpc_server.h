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

#ifndef _ARPC_SERVER_H
#define _ARPC_SERVER_H

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include "base_log.h"
#include "arpc_com.h"
#include "arpc_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SERVER_CTX(server_fd, usr_ctx)	\
struct arpc_server_handle *server_fd = (struct arpc_server_handle *)usr_ctx;

enum arpc_work_status{
	ARPC_WORK_STA_INIT = 0, 	 //初始化完毕
	ARPC_WORK_STA_RUN, 		//运行
	ARPC_WORK_STA_EXIT, 		//运行
};

#define ARPC_WROK_MAGIC	1566323

struct arpc_server_work{
	QUEUE     	       q;
	uint32_t		   magic;
	int32_t			   affinity;				/* 绑定CPU */
	struct arpc_cond   cond;					/* 数据接收的条件变量*/
	struct xio_server	*work;
	struct xio_context	*work_ctx;			/* server thread 上下文 */
	void 	*usr_context;			// 用户上下文
	work_handle_t 		thread_handle;
	enum arpc_work_status status;
	uint32_t		msg_head_max_len;
	uint64_t		msg_data_max_len;
	char 	uri[URI_MAX_LEN];
	char    ex_ctx[0];			/* exterd handle */
};

struct arpc_server_handle{
	QUEUE       q_work;
	QUEUE       q_session;
	void 		*threadpool;
	uint32_t 	work_num;
	uint32_t	iov_max_len;
	struct arpc_cond  cond;
	struct arpc_mutex  lock;
	struct arpc_session_ops	 ops;
	struct xio_server		*server;
	struct xio_context		*server_ctx;
	int (*new_session_start)(const struct arpc_new_session_req *, struct arpc_new_session_rsp *, void*);
	int (*new_session_end)(arpc_session_handle_t, struct arpc_new_session_rsp *, void*);
	int (*session_teardown)(const arpc_session_handle_t, void *usr_server_ctx, void *usr_session_ctx);
	void 	*usr_context;			// 用户上下文
	char 	uri[URI_MAX_LEN];
	uint32_t	msg_head_max_len;
	uint64_t	msg_data_max_len;
	uint32_t	msg_iov_max_len;
	uint32_t		is_stop;
	uint32_t 	session_num;
	char    ex_ctx[0];			/* exterd handle */
};

struct arpc_server_handle *arpc_create_server(uint32_t ex_ctx_size);
int arpc_destroy_server(struct arpc_server_handle* svr);
int server_insert_work(struct arpc_server_handle *server, struct arpc_server_work *work);
int server_remove_work(struct arpc_server_handle *server, struct arpc_server_work *work);
int server_insert_session(struct arpc_server_handle *server, struct arpc_session_handle *session);
int server_remove_session(struct arpc_server_handle *server, struct arpc_session_handle *session);

struct arpc_server_work *arpc_create_xio_server_work(const struct arpc_con_info *con_param, 
													struct arpc_server_handle* server, 
													struct xio_session_ops *work_ops,
													uint32_t index);
int  arpc_destroy_xio_server_work(struct arpc_server_work *work);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */
