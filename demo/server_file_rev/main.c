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
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <mcheck.h>

#include "arpc_com.h"
#include "arpc_api.h"
#include "base_log.h"
#include "sha256.h"

static char rsp_header[] = "<file:rsp>";
static char rsp_iov_data[] = "<iov:rsp>.....................";

#define SERVER_LOG(format, arg...) fprintf(stderr, "[ SERVER ]"format"\n",##arg)

void conver_hex_to_str(BYTE *hex, int hex_len, BYTE *str, int str_len)
{
	int i;
	if(hex_len*2 >= str_len){
		SERVER_LOG("buf invalid.");
		return;
	}
	for(i = 0; i < hex_len; i++){
		snprintf((char*)(str + i*2),  str_len, "%02x", hex[i]);
	}
	str[str_len -1] = '\0';
	return;
}

static void *mem_alloc(uint32_t size, void *usr_context)
{
	void *mem = malloc(size);
	return mem;
}
static int mem_free(void *buf_ptr, void *usr_context)
{
	if(buf_ptr)
		free(buf_ptr);
	return 0;
}

static int process_rx_header(struct arpc_header_msg *header, void* usr_context, uint32_t *flag)
{
	SET_METHOD(*flag, METHOD_ALLOC_DATA_BUF);// vec数据释放
	return 0;
}

static int process_rx_data(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void *usr_context)
{
	char file_path[512] = {0};
	uint32_t i;
	struct arpc_vmsg *rsp_data;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;

	if (!req_iov || !usr_context){
		SERVER_LOG("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);

	SERVER_LOG("------data sha256:%s, receive len:%lu.\n", (char*)req_iov->head, req_iov->total_data);

	sha256_init(&ctx);
	for(i = 0; i < req_iov->vec_num; i++){
		sha256_update(&ctx, req_iov->vec[i].data, req_iov->vec[i].len);
	}
	sha256_final(&ctx, buf);

	conver_hex_to_str(buf, sizeof(buf), buf_str, sizeof(buf_str));
	SERVER_LOG("sha256:%s.", (char*)buf_str);

	rsp_data = mem_alloc(sizeof(struct arpc_vmsg), NULL);
	memset(rsp_data, 0, sizeof(struct arpc_vmsg));

	rsp_data->head_len = sizeof(buf_str);
	rsp_data->head = mem_alloc(rsp_data->head_len, NULL);
	memcpy(rsp_data->head, buf_str, sizeof(buf_str));

	rsp_data->vec_num = 1;
	rsp_data->vec = mem_alloc(rsp_data->vec_num * sizeof(struct arpc_iov), NULL);
	for(i = 0; i < rsp_data->vec_num; i++){
		rsp_data->vec[i].data = mem_alloc(sizeof(buf_str), NULL);
		memcpy(rsp_data->vec[i].data, buf_str, sizeof(buf_str));
		rsp_data->vec[i].len = sizeof(buf_str);
		rsp_data->total_data +=rsp_data->vec[i].len;
	}
	rsp->rsp_iov = rsp_data;
	rsp->flags = 0;
	return 0;
}
static int release_rsp(struct arpc_vmsg *rsp_iov, void *usr_context)
{
	int i;
	if(!rsp_iov){
		SERVER_LOG("null rsp fail.\n");
		return -1;
	}
	if (rsp_iov->head) {
		mem_free(rsp_iov->head, NULL);
		rsp_iov->head =NULL;
	}

	for(i = 0; i < rsp_iov->vec_num; i++){
		if(rsp_iov->vec[i].data){
			mem_free(rsp_iov->vec[i].data, NULL);
		}
	}
	if(rsp_iov->vec){
		mem_free(rsp_iov->vec, NULL);
	}
	if(rsp_iov){
		mem_free(rsp_iov, NULL);
	}
	return 0;
}

int new_session_start(const struct arpc_new_session_req *client, struct arpc_new_session_rsp *param, void* usr_context)
{
	char file_path[512] = {0};
	FILE *fp = NULL;
	if (client && client->client_data.data){
		memcpy(usr_context, client->client_data.data, client->client_data.len);
		sprintf(file_path, "./rev_%s", (char *)usr_context);
		fp = fopen(file_path, "w");
		if (!fp){
			SERVER_LOG("fopen path:%s fail.\n", file_path);
			return 0;
		}
		fclose(fp);
	}
	return 0;
}

static arpc_session_handle_t session_fd = NULL;
int new_session_end(arpc_session_handle_t fd, struct arpc_new_session_rsp *param, void* usr_context)
{
	session_fd = fd;
	return 0;
}


static int process_async(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void* usr_context)
{
	char file_path[512] = {0};
	uint32_t i;
	struct arpc_vmsg *rsp_data;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;
	if (!req_iov || !usr_context){
		SERVER_LOG("null inputn");
		return 0;
	}

	SERVER_LOG("----data sha256:%s, receive len:%lu, vec_num:%u.\n", (char*)req_iov->head, req_iov->total_data, req_iov->vec_num);

	sha256_init(&ctx);
	for(i = 0; i < req_iov->vec_num; i++){
		sha256_update(&ctx, req_iov->vec[i].data, req_iov->vec[i].len);
	}
	sha256_final(&ctx, buf);

	conver_hex_to_str(buf, sizeof(buf), buf_str, sizeof(buf_str));
	SERVER_LOG("cal sha256:%s.", (char*)buf_str);

	assert(strncmp((char *)buf_str, req_iov->head, 64) == 0);

	rsp_data = mem_alloc(sizeof(struct arpc_vmsg), NULL);
	memset(rsp_data, 0, sizeof(struct arpc_vmsg));

	rsp_data->head_len = sizeof(buf_str);
	rsp_data->head = mem_alloc(rsp_data->head_len, NULL);
	memcpy(rsp_data->head, buf_str, sizeof(buf_str));

	rsp_data->vec_num = 1;
	rsp_data->vec = mem_alloc(rsp_data->vec_num * sizeof(struct arpc_iov), NULL);
	for(i = 0; i < rsp_data->vec_num; i++){
		rsp_data->vec[i].data = mem_alloc(sizeof(buf_str), NULL);
		memcpy(rsp_data->vec[i].data, buf_str, sizeof(buf_str));
		rsp_data->vec[i].len = sizeof(buf_str);
		rsp_data->total_data +=rsp_data->vec[i].len;
	}
	rsp->rsp_iov = rsp_data;
	rsp->flags = 0;
	//usleep(1000*((uint64_t)rsp_data %100));
	return 0;
}
static int process_rx_oneway_data(const struct arpc_vmsg *req_iov, uint32_t *flags, void *usr_context)
{
	char file_path[512] = {0};
	uint32_t i;
	if (!req_iov || !usr_context){
		SERVER_LOG("null inputn");
		return 0;
	}

	SERVER_LOG("sync------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);
	SERVER_LOG("------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);

	return 0;
}

static int process_oneway_async(const struct arpc_vmsg *req_iov, uint32_t *flags, void* usr_context)
{
	uint32_t i;
	int ret;
	struct arpc_vmsg *rsp_data;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;

	if (!req_iov || !usr_context){
		SERVER_LOG("null inputn");
		return 0;
	}

	SERVER_LOG("async-----data sha256:%s, receive len:%lu.\n", (char*)req_iov->head, req_iov->total_data);

	sha256_init(&ctx);
	for(i = 0; i < req_iov->vec_num; i++){
		sha256_update(&ctx, req_iov->vec[i].data, req_iov->vec[i].len);
	}
	sha256_final(&ctx, buf);

	conver_hex_to_str(buf, sizeof(buf), buf_str, sizeof(buf_str));
	SERVER_LOG("cal sha256:%s.", (char*)buf_str);

	assert(strncmp((char *)buf_str, req_iov->head, 64) == 0);

	rsp_data = mem_alloc(sizeof(struct arpc_vmsg), NULL);
	memset(rsp_data, 0, sizeof(struct arpc_vmsg));

	rsp_data->head_len = sizeof(buf_str);
	rsp_data->head = mem_alloc(rsp_data->head_len, NULL);
	memcpy(rsp_data->head, buf_str, sizeof(buf_str));

	rsp_data->vec_num = 1;
	rsp_data->vec = mem_alloc(rsp_data->vec_num * sizeof(struct arpc_iov), NULL);
	for(i = 0; i < rsp_data->vec_num; i++){
		rsp_data->vec[i].data = mem_alloc(sizeof(buf_str), NULL);
		memcpy(rsp_data->vec[i].data, buf_str, sizeof(buf_str));
		rsp_data->vec[i].len = sizeof(buf_str);
		rsp_data->total_data +=rsp_data->vec[i].len;
	}
	SERVER_LOG("arpc_send_oneway_msg:%s.", (char*)buf_str);
	ret = arpc_send_oneway_msg(session_fd, rsp_data, release_rsp, NULL);
	if(ret){
		SERVER_LOG("arpc_send_oneway_msg:%s fail.", (char*)buf_str);
		release_rsp(rsp_data, NULL);
	}
	return 0;
}
/*---------------------------------------------------------------------------*/
/* main									     */
/*---------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	struct arpc_server_param param;
	arpc_session_handle_t fd = NULL;
	char file_name[256] = "file_rx";
	struct aprc_option opt = {0};
	struct arpc_session_ops ops ={
		.req_ops = {
			.alloc_cb = &mem_alloc,
			.free_cb = &mem_free,
			.proc_head_cb = &process_rx_header,
			.proc_data_cb = &process_rx_data,
			.proc_async_cb = &process_async,
			.release_rsp_cb = &release_rsp,
		},
		.oneway_ops = {
			.alloc_cb = &mem_alloc,
			.free_cb = &mem_free,
			.proc_head_cb = &process_rx_header,
			.proc_data_cb = &process_rx_oneway_data,
			.proc_async_cb = &process_oneway_async,
		}
	};
	SERVER_LOG("null inputn------------------");
	if (argc < 2) {
		SERVER_LOG("Usage: %s <host> <port>n", argv[0]);
		return 0;
	}
	SET_FLAG(opt.control, ARPC_E_CTRL_CRC); //开启通信CRC检查
	opt.thread_max_num = 32;
	arpc_init_r(&opt);
	memset(&param, 0, sizeof(param));
	param.con.type = ARPC_E_TRANS_TCP;
	memcpy(param.con.ipv4.ip, argv[1], IPV4_MAX_LEN);
	param.con.ipv4.port = atoi(argv[2]);

	param.work_num = 4;
	param.default_ops = ops;
	param.new_session_start = &new_session_start;
	param.new_session_end = &new_session_end;
	param.default_ops_usr_ctx = file_name;

	fd = arpc_server_create(&param);
	if(!fd){
		goto end;
	}
	arpc_server_loop(fd, -1);

end:
	if(fd)
		arpc_server_destroy(fd);
	arpc_finish();
	return 0;
}

