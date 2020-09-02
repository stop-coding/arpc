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

#ifndef _ARPC_COM_H
#define _ARPC_COM_H

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "base_log.h"

#include "libxio.h"
#include "arpc_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARPC_ERROR		-1
#define ARPC_SUCCESS	0

#define ARPC_LOG_ERROR(format, arg...) BASE_LOG_ERROR(format, ##arg)
#define ARPC_LOG_NOTICE(format, arg...) BASE_LOG_NOTICE(format, ##arg)
#define ARPC_LOG_DEBUG(format, arg...) 	BASE_LOG_DEBUG(format,  ##arg)

#define ARPC_ASSERT(condition, format, arg...) BASE_ASSERT(condition, format, ##arg)

#define URI_MAX_LEN 256
#define MAX_DATA_SEG_LEN 1024
#define DEFAULT_DEPTH 4

#define RETRY_MAX_TIME	10	/* session断开自动重连次数*/
#define SERVER_DOWN_WAIT_TIME		10

#define IS_SET(flag, tag) (flag&(1<<tag))
#define SET_FLAG(flag, tag) flag=(flag|(1<<tag))
#define CLR_FLAG(flag, tag) flag=(flag&~(1<<tag))

#define XIO_MSG_REQ 			1
#define XIO_MSG_RSP 			2
#define XIO_MSG_CANCEL 			3
#define XIO_MSG_ALLOC_BUF 		4
#define XIO_SEND_MSG_ALLOC_BUF 	5
#define XIO_RSP_IOV_ALLOC_BUF  	6
#define XIO_SEND_END_TO_NOTIFY  7

#define MSG_SET_REQ(flag) flag=(flag|(1<<XIO_MSG_REQ))
#define MSG_SET_RSP(flag) flag=(flag|(1<<XIO_MSG_RSP))
#define MSG_CLR_REQ(flag) flag=(flag&~(1<<XIO_MSG_REQ))
#define MSG_CLR_RSP(flag) flag=(flag&~(1<<XIO_MSG_RSP))

#define IS_SET_RSP(flag) (flag&(1<<XIO_MSG_RSP))
#define IS_SET_REQ(flag) (flag&(1<<XIO_MSG_REQ))

#define TO_FREE_USER_DATA_BUF(ops, usr_ctx, iov, iov_num, i)	\
do{\
	for(i = 0; i < iov_num; i++){\
		if(iov[i].data)\
			ops(iov[i].data, usr_ctx);\
		iov[i].data = NULL;\
	}\
}while(0);

#define FLAG_ALLOC_BUF_TO_REV 		5
#define FLAG_RSP_USER_DATA 			6
#define FLAG_MSG_ERROR_DISCARD_DATA 7

enum session_status{
	SESSION_STA_INIT = 0, 	 //初始化完毕
	SESSION_STA_RUN, 		//运行
	SESSION_STA_RUN_ACTION, //活跃，指链路联通
	SESSION_STA_WAIT,
	SESSION_STA_CLEANUP, 	//释放
};

enum session_type{
	SESSION_CLIENT = 0, //
	SESSION_SERVER, 	//初始化
	SESSION_SERVER_CHILD, 	//初始化
};

struct arpc_handle_ex {
	enum session_type type;
	struct xio_connection		*active_conn;			/* connection 资源*/
	struct xio_context			*ctx;					/* session thread 上下文 */
	void 						*usr_context;			// 用户上下文
	char 						uri[URI_MAX_LEN];
	int32_t						affinity;				/* lock open connection */
	int32_t						retry_time;				/* 重连次数 */
	int32_t						reconn_interval_s;		/* 重连间隔 */
	enum session_status			status;
	pthread_cond_t 				cond;					/* 数据接收的条件变量*/
	pthread_mutex_t             lock;	    			/* lock */
	char 						handle_ex[0];			/* exterd handle */
};

struct arpc_msg_buf {
	struct xio_iovec_ex 		*xio_buf;
	struct arpc_iov				*usr_buf;
	uint32_t					depth;					// 申请发送buf分段深度（不包含buf空间），用于大块数据传输，默认为5个分段
};


struct arpc_msg_data {
	void* (*alloc_cb)(uint32_t size, void* usr_context);
	int (*free_cb)(void* buf_ptr, void* usr_context);
	void* 						usr_ctx;				/* 用户上下文*/
	uint32_t					iov_max_len;
	struct xio_connection		*active_conn;			/* connection 资源*/
	struct xio_msg				x_msg;
	pthread_cond_t 				cond;					/* 数据接收的条件变量*/
	pthread_mutex_t             lock;	    			/* lock */
	uint32_t 		 			flag;
};

struct _async_proc_ops{
	void* (*alloc_cb)(uint32_t size, void* usr_context);
	int (*free_cb)(void* buf_ptr, void* usr_context);
	int (*proc_async_cb)(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp_iov, void* usr_context);
	int (*release_rsp_cb)(struct arpc_vmsg *rsp_iov, void* usr_context);
	int (*proc_oneway_async_cb)(const struct arpc_vmsg *, uint32_t *flags, void* usr_context);
};

struct _proc_header_func{
	void* (*alloc_cb)(uint32_t size, void* usr_context);
	int (*free_cb)(void* buf_ptr, void* usr_context);
	int (*proc_head_cb)(struct arpc_header_msg *header, void* usr_context, uint32_t *flag);
};

struct _rsp_complete_ctx{
	struct arpc_vmsg *rsp_iov;
	int (*release_rsp_cb)(struct arpc_vmsg *rsp_iov, void* usr_context);
	void *rsp_usr_ctx;
};

static inline void arpc_sleep(uint64_t s)
{
	struct timeval time;
	time.tv_sec = s;
	time.tv_usec = 0;
	select(0, NULL, NULL, NULL, &time);
	return;
}

static inline void arpc_usleep(uint64_t us)
{
	struct timeval time;
	time.tv_sec = 0;
	time.tv_usec = us;
	select(0, NULL, NULL, NULL, &time);
	return;
}

// base
#define ARPC_MEM_ALLOC(size, usr_context) malloc(size)
#define ARPC_MEM_FREE(ptr, usr_context)	free(ptr)

#define SAFE_FREE_MEM(prt) do{if(prt) {ARPC_MEM_FREE(prt, NULL);prt= NULL;}}while(0);

int get_uri(const struct arpc_con_info *param, char *uri, uint32_t uri_len);
void* _arpc_get_threadpool();
int _arpc_get_ipv4_addr(struct sockaddr_storage *src_addr, char *ip, uint32_t len, uint32_t *port);

// others
void _debug_printf_msg(struct xio_msg *rsp);
int _post_iov_to_async_thread(struct arpc_vmsg *iov, struct xio_msg *oneway_msg, struct _async_proc_ops *ops, void *usr_ctx);
int _arpc_wait_request_rsp(struct arpc_msg_data* pri_msg, int32_t timeout_ms);

int _create_header_source(struct xio_msg *msg, struct _proc_header_func *ops, uint64_t iov_max_len, void *usr_ctx);
int _clean_header_source(struct xio_msg *msg, mem_free_cb_t free_cb, void *usr_ctx);

int _do_respone(struct arpc_vmsg *rsp_iov, struct xio_msg  *req, rsp_cb_t release_rsp_cb, void *rsp_ctx);

// request
int _process_request_header(struct xio_msg *msg, struct request_ops *ops, uint64_t iov_max_len, void *usr_ctx);
int _process_request_data(struct xio_msg *req, struct request_ops *ops, int last_in_rxq, void *usr_ctx);
int _process_send_rsp_complete(struct xio_msg *rsp, struct request_ops *ops, void *usr_ctx);

// response
int _process_rsp_header(struct xio_msg *rsp, void *usr_ctx);
int _process_rsp_data(struct xio_msg *rsp, int last_in_rxq);

// oneway
int _process_oneway_header(struct xio_msg *msg, struct oneway_ops *ops, uint64_t iov_max_len, void *usr_ctx);
int _process_oneway_data(struct xio_msg *req, struct oneway_ops *ops, int last_in_rxq, void *usr_ctx);

// make request
int _arpc_rev_request_head(struct xio_msg *in_rsp);
int _arpc_rev_request_rsp(struct xio_msg *in_rsp);
int _release_rsp_msg(struct arpc_msg *msg);
int _process_send_complete(struct arpc_msg *msg);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */
