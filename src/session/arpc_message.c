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
#include "arpc_request.h"
#include "arpc_message.h"
#include "arpc_proto.h"

static int free_receive_msg_buf(struct arpc_msg *msg);

static void *arpc_msg_alloc(uint32_t size, void *usr_context)
{
	void *mem = malloc(size);
	return mem;
}
static int arpc_msg_free(void *buf_ptr, void *usr_context)
{
	if(buf_ptr)
		free(buf_ptr);
	return 0;
}

struct arpc_msg *arpc_new_msg(const struct arpc_msg_param *p)
{
	struct arpc_msg *ret_msg = NULL;
	struct arpc_msg_ex *ex_msg = NULL;
	int ret;

	ret_msg = (struct arpc_msg*)arpc_mem_alloc(sizeof(struct arpc_msg) + sizeof(struct arpc_msg_ex),NULL);
	LOG_THEN_RETURN_VAL_IF_TRUE(!ret_msg, NULL, "arpc_mem_alloc arpc_msg fail.");

	memset(ret_msg, 0, sizeof(struct arpc_msg) + sizeof(struct arpc_msg_ex));
	ex_msg = (struct arpc_msg_ex *)ret_msg->handle;
	ex_msg->flags = 0;	// 暂时不使用用户的flag
	if (p){
		ex_msg->alloc_cb = p->alloc_cb;
		ex_msg->free_cb = p->free_cb;
		ex_msg->usr_context = p->usr_context;
		ex_msg->iov_max_len = p->msg_iov_max_len? p->msg_iov_max_len:0;
	}else{
		ex_msg->alloc_cb = &arpc_msg_alloc;
		ex_msg->free_cb = &arpc_msg_free;
		ex_msg->usr_context =NULL;
		ex_msg->iov_max_len = 0;
	}
	ex_msg->msg = ret_msg;
	return ret_msg;
}


int arpc_delete_msg(struct arpc_msg **msg)
{
	struct arpc_msg_ex *ex_msg;
	struct arpc_msg *free_msg;
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");
	LOG_THEN_RETURN_VAL_IF_TRUE(!*msg, ARPC_ERROR, "*msg is null.");
	
	free_msg = *msg;
	ex_msg = (struct arpc_msg_ex*)free_msg->handle;
	if (IS_SET(ex_msg->flags, XIO_MSG_REQ)) {
		ARPC_LOG_ERROR("can't delete msg that be do request.");
		return -1;
	}

	if (IS_SET(ex_msg->flags, XIO_MSG_RSP)) {
		free_receive_msg_buf(free_msg);
	}
	if(free_msg->clean_send_cb){
		free_msg->clean_send_cb(&free_msg->send, free_msg->send_ctx);
		free_msg->clean_send_cb = NULL;
	}

	if(free_msg->proc_rsp_cb){
		free_msg->proc_rsp_cb(NULL, free_msg->receive_ctx);
	}

	SAFE_FREE_MEM(*msg);

	return 0;
}

int arpc_reset_msg(struct arpc_msg *msg)
{
	struct arpc_msg_ex *ex_msg;
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");

	ex_msg = (struct arpc_msg_ex*)msg->handle;
	if (IS_SET(ex_msg->flags, XIO_MSG_REQ)) {
		ARPC_LOG_ERROR("can't reset msg that be do request.");
		return -1;
	}

	if (IS_SET(ex_msg->flags, XIO_MSG_RSP)) {
		free_receive_msg_buf(msg);
	}
	ex_msg->flags = 0;
	if(msg->clean_send_cb){
		msg->clean_send_cb(&msg->send, msg->send_ctx);
		msg->clean_send_cb = NULL;
	}
	if(msg->proc_rsp_cb){
		msg->proc_rsp_cb(NULL, msg->receive_ctx);
	}
	msg->proc_rsp_cb = NULL;

	return 0;
}

static int free_receive_msg_buf(struct arpc_msg *msg)
{
	struct arpc_msg_ex *ex_msg;
	uint32_t i;

	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");
	ex_msg = (struct arpc_msg_ex*)msg->handle;
	if(IS_SET(ex_msg->flags, XIO_RSP_IOV_ALLOC_BUF)){
		free_msg_xio2arpc(&msg->receive, ex_msg->free_cb, ex_msg->usr_context);
		CLR_FLAG(ex_msg->flags, XIO_RSP_IOV_ALLOC_BUF);
	}
	
	return 0;
}
