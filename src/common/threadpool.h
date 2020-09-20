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

#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include <stdio.h>
#include <errno.h>


#ifdef __cplusplus
extern "C" {
#endif

#define TP_ERROR				-1
#define TP_SUCCESS		 		 0


#define WORK_DONE_AUTO_FREE 	 1
#define WORK_DONE_MANUAL_FREE 	 0

typedef void *tp_handle;

struct tp_param {
	uint32_t thread_max_num;			/* 线程池的线程数*/
	uint32_t max_stack_size;		/* 线程栈空间大小*/
	uint32_t flag;
};

tp_handle tp_create_thread_pool(struct tp_param *p);
int tp_destroy_thread_pool(tp_handle *fd);

typedef void *work_handle_t;

struct tp_thread_work {
	int (*loop)(void *usr_ctx);		/* 循环回调函数*/
	void (*stop)(void *usr_ctx);		/* 停止循环回调函数*/
	void *usr_ctx;						/* 任务上下文*/
};

work_handle_t tp_post_one_work(tp_handle fd, struct tp_thread_work *w, uint8_t auto_free);
int tp_wait_work_done(work_handle_t *w, uint32_t timeout_ms);
int tp_cancel_one_work(work_handle_t *w);
uint64_t tp_get_work_thread_id(work_handle_t w);
uint32_t tp_get_pool_idle_num(tp_handle fd);
#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */
