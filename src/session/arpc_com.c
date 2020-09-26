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
#include <arpa/inet.h>

#include "arpc_com.h"
#include "threadpool.h"
#include "arpc_response.h"

struct aprc_paramter{
	uint8_t is_init;
	uint32_t thread_max_num;
	tp_handle thread_pool;
};

static struct aprc_paramter g_param= {
	.thread_max_num = 30,	//默认是个线程
	.thread_pool	= NULL,
};

static const char SERVER_DEFAULT[] = "rsp-header:undefine";

int get_uri(const struct arpc_con_info *param, char *uri, uint32_t uri_len)
{
	const char *type = NULL, *ip=NULL;
	uint32_t port = 0;
	if (!param || !uri) {
		return -1;
	}
	switch(param->type){
		case ARPC_E_TRANS_TCP:
			type = "tcp";
			ip = param->ipv4.ip;
			port = param->ipv4.port;
			break;
		default:
			ARPC_LOG_ERROR("unkown type:[%d].", param->type);
			return -1;
	}
	(void)sprintf(uri, "%s://%s:%u", type, ip, port);
	ARPC_LOG_NOTICE("uri:[%s].", uri);
	return 0;
}

int arpc_get_ipv4_addr(struct sockaddr_storage *src_addr, char *ip, uint32_t len, uint32_t *port)
{
	struct sockaddr_in *s4;
	LOG_THEN_RETURN_VAL_IF_TRUE((!src_addr || !ip || !port), ARPC_ERROR, "input null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((src_addr->ss_family != AF_INET || len < INET_ADDRSTRLEN), ARPC_ERROR, "input invalid.");

	s4 = (struct sockaddr_in *)src_addr;
	*port = s4->sin_port;
	inet_ntop(AF_INET, s4, ip, len);
	return ARPC_SUCCESS;
}

int arpc_init_r(struct aprc_option *opt)
{
	struct tp_param p = {0};
	ARPC_LOG_DEBUG( "arpc_init.");
	if (g_param.is_init){
		ARPC_LOG_NOTICE( "arpc global init have done.");
		return 0;
	}
	g_param.is_init = 1;
	xio_init();
	if (opt) {
		//p.thread_max_num = (opt->thread_max_num > 1)?opt->thread_max_num:2;
		//p.cpu_max_num = (opt->cpu_max_num > 1)?opt->cpu_max_num:4;
		p.thread_max_num = 32;
	}else{
		//p.thread_max_num = g_param.thread_max_num;
		p.thread_max_num = 32;
	}
	g_param.thread_pool = tp_create_thread_pool(&p);
	return 0;
}
int arpc_init()
{
	struct tp_param p = {0};
	ARPC_LOG_DEBUG( "arpc_init.");
	if (g_param.is_init){
		ARPC_LOG_NOTICE( "arpc global init have done.");
		return 0;
	}
	g_param.is_init = 1;
	xio_init();
	p.thread_max_num = 32;
	g_param.thread_pool = tp_create_thread_pool(&p);
	return 0;
}

void arpc_finish()
{
	ARPC_LOG_DEBUG( "arpc_finish.");
	if (!g_param.is_init){
		ARPC_LOG_NOTICE( "arpc global init have not done.");
		return;
	}
	g_param.is_init = 0;
	tp_destroy_thread_pool(&g_param.thread_pool);
	xio_shutdown();
}

tp_handle arpc_get_threadpool()
{
	return g_param.thread_pool;
}

uint32_t arpc_thread_max_num()
{
	return g_param.thread_max_num;
}

void debug_printf_msg(struct xio_msg *rsp)
{
	struct xio_iovec	*sglist = vmsg_base_sglist(&rsp->in);
	char			*str;
	uint32_t		nents = vmsg_sglist_nents(&rsp->in);
	uint32_t		len, i;
	char 			tmp;
	str = (char *)rsp->in.header.iov_base;
	len = rsp->in.header.iov_len;
	if (str) {
		tmp = str[len -1];
		str[len -1] = '\0';
		ARPC_LOG_DEBUG("message header : [%llu] - %s.",(unsigned long long)(rsp->sn + 1), str);
		str[len -1] = tmp;
	}
	for (i = 0; i < nents; i++) {
		str = (char *)sglist[i].iov_base;
		len = sglist[i].iov_len;
		if (str) {
			tmp = str[len -1];
			str[len -1] = '\0';
			ARPC_LOG_DEBUG("message data: [%llu][%d][%d] - %s\n",
					(unsigned long long)(rsp->sn + 1),
					i, len, str);
			str[len -1] = tmp;
		}
	}
	return;
}

int post_to_async_thread(struct arpc_thread_param *param)
{
	struct tp_thread_work thread;
	LOG_THEN_RETURN_VAL_IF_TRUE((!param), ARPC_ERROR, "param null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!param->loop), ARPC_ERROR, "loop null.");
	thread.loop = param->loop;
	thread.stop = NULL;
	thread.usr_ctx = (void*)param;
	if(!tp_post_one_work(arpc_get_threadpool(), &thread, WORK_DONE_AUTO_FREE)){
		ARPC_LOG_ERROR( "tp_post_one_work error.");
		return -1;
	}
	return 0;
}

int create_xio_msg_usr_buf(struct xio_msg *msg, struct proc_header_func *ops, uint64_t iov_max_len, void *usr_ctx)
{
	struct xio_iovec	*sglist;
	uint32_t			nents = 0;
	struct arpc_header_msg header;
	uint32_t flag = 0;
	uint32_t i;
	int ret;
	uint64_t last_size;

	msg->in.sgl_type = XIO_SGL_TYPE_IOV;
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg->in.header.iov_base || !msg->in.header.iov_len), -1, "header null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!msg->in.header.iov_len), -1, "header len is 0.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!ops->proc_head_cb), -1, "proc_head_cb null.");

	memset(&header, 0, sizeof(struct arpc_header_msg));
	header.head = msg->in.header.iov_base;
	header.head_len = msg->in.header.iov_len;
	header.data_len = msg->in.total_data_len;
	// header process
	msg->usr_flags = 0;
	ret = ops->proc_head_cb(&header, usr_ctx, &flag);
	if (ret != ARPC_SUCCESS || !msg->in.total_data_len){
		//SET_FLAG(msg->usr_flags, FLAG_MSG_ERROR_DISCARD_DATA); // data数据不做处理
		ARPC_LOG_DEBUG("discard data, total_data_len[%lu].", msg->in.total_data_len);
		return ARPC_ERROR;
	}
	msg->usr_flags = flag;
	// alloc data buf form user define call back
	/*if (!IS_SET(msg->usr_flags, METHOD_ALLOC_DATA_BUF)) {
		ARPC_LOG_ERROR("not need alloc data buf.");
		return ARPC_ERROR;
	}*/
	if (!ops->alloc_cb || !ops->free_cb) {
		CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
		ARPC_LOG_ERROR("func malloc or free is null.");
		return ARPC_ERROR;
	}

	// 分配内存
	last_size = msg->in.total_data_len%iov_max_len;
	nents = (last_size)? 1: 0;
	nents += (msg->in.total_data_len / iov_max_len);
	sglist = (struct xio_iovec* )ARPC_MEM_ALLOC(nents * sizeof(struct xio_iovec), NULL);
	last_size = (last_size)? last_size :iov_max_len;

	ARPC_LOG_DEBUG("get msg, nent:%u, iov_max_len:%lu, total_size:%lu, sglist:%p", nents, iov_max_len, msg->in.total_data_len, sglist);
	for (i = 0; i < nents - 1; i++) {
		sglist[i].iov_len = iov_max_len;
		sglist[i].iov_base = ops->alloc_cb(sglist[i].iov_len, usr_ctx);
		LOG_THEN_GOTO_TAG_IF_VAL_TRUE((!sglist[i].iov_base), error_1, "calloc fail.");
	}
	sglist[i].iov_len = last_size;
	sglist[i].iov_base = ops->alloc_cb(sglist[i].iov_len, usr_ctx);
	ARPC_LOG_DEBUG("i:%u ,data:%p, len:%lu.",i, sglist[i].iov_base, sglist[i].iov_len);
	// 出参
	msg->in.data_tbl.sglist = (void*)sglist;
	vmsg_sglist_set_nents(&msg->in, nents);
	msg->in.sgl_type		= XIO_SGL_TYPE_IOV_PTR;
	SET_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
	return 0;

error_1:
	for (i = 0; i < nents; i++) {
		if (sglist[i].iov_base)
			ops->free_cb(sglist[i].iov_base, usr_ctx);
		sglist[i].iov_base =NULL;
	}
	if (sglist) {
		ARPC_MEM_FREE(sglist, NULL);
		sglist = NULL;
	}
	msg->in.sgl_type = XIO_SGL_TYPE_IOV;
	msg->in.total_data_len = 0;
	CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
	return -1;
}

int destroy_xio_msg_usr_buf(struct xio_msg *msg, mem_free_cb_t free_cb, void *usr_ctx)
{
	struct xio_iovec	*sglist;
	uint32_t			nents;
	uint32_t i;

	LOG_THEN_RETURN_VAL_IF_TRUE((!msg), ARPC_ERROR, "msg null.");
	LOG_THEN_RETURN_VAL_IF_TRUE((!free_cb), ARPC_ERROR, "alloc free is null.");

	// 释放内存
	nents = vmsg_sglist_nents(&msg->in);
	sglist = vmsg_base_sglist(&msg->in);
	if (!sglist || !nents){
		ARPC_LOG_DEBUG("msg buf is null, nents:%u.", nents);
		return ARPC_ERROR;
	}

	if (IS_SET(msg->usr_flags, METHOD_CALLER_HIJACK_RX_DATA)){
		return 0;
	}
	
	if (!IS_SET(msg->usr_flags, METHOD_ALLOC_DATA_BUF)) {
		ARPC_LOG_ERROR("not need free data buf.");
		return ARPC_ERROR;
	}

	for (i = 0; i < nents; i++) {
		if (sglist[i].iov_base)
			free_cb(sglist[i].iov_base, usr_ctx);
	}
	if (sglist)
		ARPC_MEM_FREE(sglist, NULL);
	
	// 出参
	msg->in.data_tbl.sglist = NULL;
	vmsg_sglist_set_nents(&msg->in, 0);
	msg->in.sgl_type	= XIO_SGL_TYPE_IOV;
	CLR_FLAG(msg->usr_flags, METHOD_ALLOC_DATA_BUF);
	return 0;
}

struct arpc_common_msg *arpc_create_common_msg(uint32_t ex_data_size)
{
	struct arpc_common_msg *req_msg = NULL;
	int ret;

	req_msg = (struct arpc_common_msg*)ARPC_MEM_ALLOC(sizeof(struct arpc_common_msg) + ex_data_size,NULL);
	LOG_THEN_RETURN_VAL_IF_TRUE(!req_msg, NULL, "ARPC_MEM_ALLOC arpc_msg fail.");
	memset(req_msg, 0, sizeof(struct arpc_common_msg) + ex_data_size);
	ret = arpc_cond_init(&req_msg->cond); 
	LOG_THEN_GOTO_TAG_IF_VAL_TRUE(ret, error, "arpc_cond_init for new msg fail.");
	req_msg->flag = 0;
	QUEUE_INIT(&req_msg->q);
	return req_msg;
error:
	SAFE_FREE_MEM(req_msg);
	return NULL;
}


int arpc_destroy_common_msg(struct arpc_common_msg *msg)
{
	int ret;
	LOG_THEN_RETURN_VAL_IF_TRUE(!msg, ARPC_ERROR, "msg is null.");
	ret = arpc_cond_lock(&msg->cond);
	LOG_THEN_RETURN_VAL_IF_TRUE(ret, ARPC_ERROR, "arpc_cond_lock null.");
	arpc_cond_unlock(&msg->cond);
	arpc_cond_destroy(&msg->cond);
	SAFE_FREE_MEM(msg);
	return 0;
}
