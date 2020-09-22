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
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>

#include "arpc_com.h"
#include "arpc_make_request.h"

struct arpc_msg *arpc_new_msg(const struct arpc_msg_param *p)
{
	struct arpc_msg *ret_msg = NULL;
	struct arpc_msg_data *pri_msg = NULL;

	ret_msg = (struct arpc_msg*)ARPC_MEM_ALLOC(sizeof(struct arpc_msg) + sizeof(struct arpc_msg_data),NULL);
	if (!ret_msg) {
		ARPC_LOG_ERROR( "calloc fail.");
		return NULL;
	}
	memset(ret_msg, 0, sizeof(struct arpc_msg) + sizeof(struct arpc_msg_data));
	pri_msg = (struct arpc_msg_data *)ret_msg->handle;
	if (!pri_msg) {
		ARPC_LOG_ERROR( "calloc handle fail.");
		goto error;
	}

	/* 变量操作 */
	pthread_mutex_init(&pri_msg->lock, NULL); /* 初始化互斥锁 */
    pthread_cond_init(&pri_msg->cond, NULL);	/* 初始化条件变量 */

	pri_msg->flag = 0;	// 暂时不使用用户的flag
	pri_msg->x_msg.user_context = (void *)ret_msg;
	if (p){
		pri_msg->alloc_cb = p->alloc_cb;
		pri_msg->free_cb = p->free_cb;
		pri_msg->usr_ctx = p->usr_context;
	}
	pri_msg->iov_max_len = IOV_DEFAULT_MAX_LEN;

	return ret_msg;
error:
	pthread_cond_destroy(&pri_msg->cond);
	pthread_mutex_destroy(&pri_msg->lock);
	if (ret_msg)
		ARPC_MEM_FREE(ret_msg, NULL);
	return NULL;
}


int arpc_delete_msg(struct arpc_msg **msg)
{
	struct arpc_msg_data *pri_msg = NULL;
	if (!msg && !(*msg)) {
		ARPC_LOG_ERROR( "msg null, fail.");
		return ARPC_ERROR;
	}
	pri_msg = (struct arpc_msg_data*)(*msg)->handle;
	pthread_mutex_lock(&pri_msg->lock);
	if(IS_SET(pri_msg->flag, XIO_MSG_REQ)){
		pthread_mutex_unlock(&pri_msg->lock);
		ARPC_LOG_ERROR("message is do request, need to wait respone.");
		return ARPC_ERROR;
	}
	SET_FLAG(pri_msg->flag, XIO_MSG_CANCEL);
	ARPC_LOG_DEBUG("message  do cancel.");
	pthread_cond_broadcast(&pri_msg->cond); // 释放信号,让等待回复消息线程退出
	pthread_mutex_unlock(&pri_msg->lock);

	pthread_mutex_lock(&pri_msg->lock);
	if (IS_SET(pri_msg->flag, XIO_MSG_RSP)) {
		ARPC_LOG_DEBUG("message get rsp, need to release.");
		_release_rsp_msg(*msg); // 释放回复资源
	}
	CLR_FLAG(pri_msg->flag, XIO_MSG_RSP);
	pthread_cond_destroy(&pri_msg->cond);
	pthread_mutex_unlock(&pri_msg->lock);

	pthread_mutex_destroy(&pri_msg->lock);
	if(*msg)
		ARPC_MEM_FREE(*msg, NULL);
	*msg = NULL;
	return 0;
}

int arpc_reset_msg(struct arpc_msg *msg)
{
	struct arpc_msg_data *pri_msg = NULL;
	if (!msg) {
		ARPC_LOG_ERROR( "msg null, fail.");
		return ARPC_ERROR;
	}
	pri_msg = (struct arpc_msg_data*)msg->handle;
	pthread_mutex_lock(&pri_msg->lock);
	if(IS_SET(pri_msg->flag, XIO_MSG_CANCEL)){
		ARPC_LOG_ERROR("message is canceling.");
		goto end;
	}
	
	if (IS_SET(pri_msg->flag, XIO_MSG_REQ)) {
		ARPC_LOG_ERROR("message do reuqest.");
		goto end;
	}

	if (IS_SET(pri_msg->flag, XIO_MSG_RSP)) {
		ARPC_LOG_DEBUG("message get rsp, need to release.");
		_release_rsp_msg(msg); // 释放回复资源
	}
	CLR_FLAG(pri_msg->flag, XIO_MSG_RSP);
	pri_msg->flag = 0;

end:
	pthread_mutex_unlock(&pri_msg->lock);
	return 0;
}
