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
#include <sys/time.h>
#include <pthread.h>

#include "base_log.h"
#include "queue.h"
#include "threadpool.h"

#include "libxio.h"
#include "arpc_api.h"
#include "arpc_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARPC_TIME_OUT	-2
#define ARPC_ERROR		-1
#define ARPC_SUCCESS	0

#define ARPC_LOG_ERROR(format, arg...) BASE_LOG_ERROR(format, ##arg)
#define ARPC_LOG_NOTICE(format, arg...) BASE_LOG_NOTICE(format, ##arg)
#define ARPC_LOG_DEBUG(format, arg...) 	BASE_LOG_DEBUG(format,  ##arg)

#define ARPC_ASSERT(condition, format, arg...) BASE_ASSERT(condition, format, ##arg)

#define URI_MAX_LEN 	256
#define MAX_DATA_SEG_LEN 1024
#define DEFAULT_DEPTH 	4

#define IS_SET(flag, tag) (flag&(tag))
#define SET_FLAG(flag, tag) flag=(flag|(tag))
#define CLR_FLAG(flag, tag) flag=(flag&~(tag))

// 消息状态标志
#define XIO_MSG_REQ 				(1<<0)
#define XIO_MSG_RSP 				(1<<1)
#define XIO_MSG_CANCEL 				(1<<2)
#define XIO_MSG_ALLOC_BUF 			(1<<3)
#define XIO_SEND_MSG_ALLOC_BUF 		(1<<4)
#define XIO_RSP_IOV_ALLOC_BUF  		(1<<5)
#define XIO_SEND_END_TO_NOTIFY  	(1<<6)
#define XIO_RELEASE_ARPC_MSG  		(1<<7)
#define XIO_MSG_FLAG_ALLOC_IOV_MEM 	(1<<8)
#define XIO_MSG_ERROR_DISCARD_DATA  (1<<9)

#define MSG_SET_REQ(flag) flag=(flag|XIO_MSG_REQ)
#define MSG_SET_RSP(flag) flag=(flag|XIO_MSG_RSP)
#define MSG_CLR_REQ(flag) flag=(flag&~XIO_MSG_REQ)
#define MSG_CLR_RSP(flag) flag=(flag&~XIO_MSG_RSP)

#define IS_SET_RSP(flag) (flag&(XIO_MSG_RSP)
#define IS_SET_REQ(flag) (flag&XIO_MSG_REQ)

// 互斥锁
struct arpc_mutex{
  pthread_mutex_t     lock;	    			/* lock */
};

inline static int arpc_mutex_init(struct arpc_mutex *m)
{
	return pthread_mutex_init(&m->lock, NULL);
}

inline static int arpc_mutex_lock(struct arpc_mutex *m)
{
	return pthread_mutex_lock(&m->lock);
}

inline static int arpc_mutex_trylock(struct arpc_mutex *m)
{
	return pthread_mutex_trylock(&m->lock);
}

inline static int arpc_mutex_unlock(struct arpc_mutex *m)
{
	return pthread_mutex_unlock(&m->lock);
}

inline static int arpc_mutex_destroy(struct arpc_mutex *m)
{
	return pthread_mutex_destroy(&m->lock);
}

// 读写锁
struct arpc_rwlock{
  pthread_rwlock_t lock;	    			/* lock */
};

inline static int arpc_rwlock_init(struct arpc_rwlock *rw)
{
	return pthread_rwlock_init(&rw->lock, NULL);
}

inline static int arpc_rwlock_rdlock(struct arpc_rwlock *rw)
{
	return pthread_rwlock_rdlock(&rw->lock);
}

inline static int arpc_rwlock_tryrdlock(struct arpc_rwlock *rw)
{
	return pthread_rwlock_tryrdlock(&rw->lock);
}

inline static int arpc_rwlock_wrlock(struct arpc_rwlock *rw)
{
	return pthread_rwlock_wrlock(&rw->lock);
}

inline static int arpc_rwlock_trywrlock(struct arpc_rwlock *rw)
{
	return pthread_rwlock_trywrlock(&rw->lock);
}

inline static int arpc_rwlock_unlock(struct arpc_rwlock *rw)
{
	return pthread_rwlock_unlock(&rw->lock);
}

inline static int arpc_rwlock_destroy(struct arpc_rwlock *rw)
{
	return pthread_rwlock_destroy(&rw->lock);
}

// 信号量
struct arpc_cond{
  pthread_cond_t cond;	    			/* cond */
  pthread_mutex_t     lock;	    			/* lock */
};

inline static int arpc_cond_init(struct arpc_cond *cond)
{
	pthread_mutex_init(&cond->lock, NULL); /* 初始化互斥锁 */
	return pthread_cond_init(&cond->cond, NULL);
}

inline static int arpc_cond_lock(struct arpc_cond *cond)
{
	return pthread_mutex_lock(&cond->lock);
}

inline static int arpc_cond_trylock(struct arpc_cond *cond)
{
	return pthread_mutex_trylock(&cond->lock);
}

inline static int arpc_cond_unlock(struct arpc_cond *cond)
{
	return pthread_mutex_unlock(&cond->lock);
}

inline static int arpc_cond_wait_timeout(struct arpc_cond *cond, uint64_t timeout_ms)
{
	int ret;
	struct timespec abstime;
	struct timeval now;
	uint64_t nsec;

	gettimeofday(&now, NULL);	// 线程安全
	nsec = now.tv_usec * 1000 + (timeout_ms % 1000) * 1000000;
	abstime.tv_sec=now.tv_sec + nsec / 1000000000 + timeout_ms / 1000;
	abstime.tv_nsec=nsec % 1000000000;
	return pthread_cond_timedwait(&cond->cond, &cond->lock, &abstime);
}

inline static int arpc_cond_wait(struct arpc_cond *cond)
{
	return pthread_cond_wait(&cond->cond, &cond->lock);
}

inline static int arpc_cond_notify(struct arpc_cond *cond)
{
	return pthread_cond_signal(&cond->cond);
}

inline static int arpc_cond_notify_all(struct arpc_cond *cond)
{
	return pthread_cond_broadcast(&cond->cond);;
}

inline static int arpc_cond_destroy(struct arpc_cond *cond)
{
	pthread_cond_destroy(&cond->cond);
	pthread_mutex_destroy(&cond->lock);
	return 0;
}

#define ARPC_MINI_IO_DATA_MAX_LEN   (2*1024)

// 最小空闲的线程数
#define ARPC_MIN_THREAD_IDLE_NUM    (4)

struct async_proc_ops{
	void* (*alloc_cb)(uint32_t size, void* usr_context);
	int (*free_cb)(void* buf_ptr, void* usr_context);
	int (*proc_async_cb)(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp_iov, void* usr_context);
	int (*release_rsp_cb)(struct arpc_vmsg *rsp_iov, void* usr_context);
	int (*proc_oneway_async_cb)(const struct arpc_vmsg *, uint32_t *flags, void* usr_context);
};

struct proc_header_func{
	void* (*alloc_cb)(uint32_t size, void* usr_context);
	int (*free_cb)(void* buf_ptr, void* usr_context);
	int (*proc_head_cb)(struct arpc_header_msg *header, void* usr_context, uint32_t *flag);
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
	if(!us){
		return;
	}
	time.tv_sec = 0;
	time.tv_usec = us;
	select(0, NULL, NULL, NULL, &time);
	return;
}

struct arpc_thread_param{
	void					*rsp_ctx;
	struct xio_msg			*req_msg;
	struct arpc_vmsg 		rev_iov;
	struct async_proc_ops	ops;
	int (*loop)(void *usr_ctx);
	void					*usr_ctx;
};

#define ARPC_COM_MSG_MAGIC 0xfa577
enum  arpc_msg_type{
	ARPC_MSG_TYPE_REQ,
	ARPC_MSG_TYPE_RSP,
	ARPC_MSG_TYPE_OW,
};

struct arpc_common_msg {
	QUEUE 						q;
	uint32_t					magic;
	int							ref;
	enum	arpc_msg_type		type;
	struct arpc_cond 			cond;				
	struct arpc_connection		*conn;
	struct xio_msg				*tx_msg;
	uint32_t 					retry_cnt;
    uint32_t                    flag;
	void 		                *usr_context;				/*! @brief 用户上下文 */
    char                        ex_data[0];
};

struct arpc_common_msg *arpc_create_common_msg(uint32_t ex_data_size);
int arpc_destroy_common_msg(struct arpc_common_msg *msg);

// base
inline static void *arpc_mem_alloc(size_t size, void *mem_ctx){
	return malloc(size);
}

inline static void arpc_mem_free(void *prt, void *mem_ctx){
	free(prt);
}

#define SAFE_FREE_MEM(prt) do{if(prt) {arpc_mem_free(prt, NULL);prt= NULL;}}while(0);

int get_uri(const struct arpc_con_info *param, char *uri, uint32_t uri_len);
const char *arpc_uri_get_resource_ptr(const char *uri);
int arpc_uri_get_portal(const char *uri, char *portal, int portal_len);
int arpc_uri_get_resource(const char *uri, char *resource, int resource_len);
int arpc_uri_get_proto(const char *uri, char *proto, int proto_len);

void* arpc_get_threadpool();
uint32_t arpc_thread_max_num();
uint32_t arpc_cpu_max_num();
int arpc_get_ipv4_addr(struct sockaddr_storage *src_addr, char *ip, uint32_t len, uint32_t *port);

// others
void debug_printf_msg(struct xio_msg *rsp);
int post_to_async_thread(struct arpc_thread_param *param);

int create_xio_msg_usr_buf(struct xio_msg *msg, struct proc_header_func *ops, uint64_t iov_max_len, void *usr_ctx);
int destroy_xio_msg_usr_buf(struct xio_msg *msg, mem_free_cb_t free_cb, void *usr_ctx);

int move_msg_xio2arpc(struct xio_vmsg *xio_msg, struct arpc_vmsg *msg, struct arpc_msg_attr *attr);
void free_msg_xio2arpc(struct arpc_vmsg *msg, mem_free_cb_t free_cb, void *usr_ctx);

int convert_msg_arpc2xio(const struct arpc_vmsg *usr_msg, struct xio_vmsg *xio_msg, struct arpc_msg_attr *attr);
void free_msg_arpc2xio(struct xio_vmsg *xio_msg);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */
