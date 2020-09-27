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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <mcheck.h>

#include "arpc_api.h"
#include "sha256.h"
#include "base_log.h"

#define BUF_MAX_SIZE 4096

void conver_hex_to_str(BYTE *hex, int hex_len, BYTE *str, int str_len)
{
	int i;
	if(hex_len*2 >= str_len){
		BASE_LOG_ERROR("buf invalid.");
		return;
	}
	for(i = 0; i < hex_len; i++){
		snprintf((char*)(str + i*2),  str_len, "%02x", hex[i]);
	}
	str[str_len -1] = '\0';
	return;
}

static char rsp_header[] = "<file:rsp>";
static char rsp_iov_data[] = "<iov:rsp>.....................";

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
	FILE *fp = NULL;
	uint32_t i;
	struct arpc_vmsg *rsp_data;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;

	if (!req_iov || !usr_context){
		BASE_LOG_ERROR("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);

	BASE_LOG_NOTICE("------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);
	BASE_LOG_NOTICE("------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);
	fp = fopen(file_path, "ab");
	if (!fp){
		BASE_LOG_ERROR("fopen path:%s fail.\n", file_path);
		return 0;
	}
	sha256_init(&ctx);
	for(i = 0; i < req_iov->vec_num; i++){
		fwrite(req_iov->vec[i].data, 1, req_iov->vec[i].len, fp);
		fseek(fp, 0, SEEK_END);
		sha256_update(&ctx, req_iov->vec[i].data, req_iov->vec[i].len);
	}
	sha256_final(&ctx, buf);
	fclose(fp);

	conver_hex_to_str(buf, sizeof(buf), buf_str, sizeof(buf_str));
	BASE_LOG_ERROR("sha256:%s.", (char*)buf_str);

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
		BASE_LOG_ERROR("null rsp fail.\n");
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
			BASE_LOG_ERROR("fopen path:%s fail.\n", file_path);
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
	FILE *fp = NULL;
	char file_path[512] = {0};
	uint32_t i;
	struct arpc_vmsg *rsp_data;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;
	if (!req_iov || !usr_context){
		BASE_LOG_ERROR("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);

	BASE_LOG_NOTICE("----dddd--file:%s, receive len:%lu.\n", file_path, req_iov->total_data);

	fp = fopen(file_path, "ab");
	if (!fp){
		BASE_LOG_ERROR("fopen path:%s fail.\n", file_path);
		return 0;
	}
	sha256_init(&ctx);
	for(i = 0; i < req_iov->vec_num; i++){
		fwrite(req_iov->vec[i].data, 1, req_iov->vec[i].len, fp);
		fseek(fp, 0, SEEK_END);
		sha256_update(&ctx, req_iov->vec[i].data, req_iov->vec[i].len);
	}
	sha256_final(&ctx, buf);
	fclose(fp);
	BASE_LOG_NOTICE("----dddd--end.\n");

	conver_hex_to_str(buf, sizeof(buf), buf_str, sizeof(buf_str));
	BASE_LOG_ERROR("sha256:%s.", (char*)buf_str);

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
	BASE_LOG_NOTICE("----dddd--end:%p.\n", rsp_data);
	rsp->flags = 0;

	return 0;
}
static int process_rx_oneway_data(const struct arpc_vmsg *req_iov, uint32_t *flags, void *usr_context)
{
	char file_path[512] = {0};
	FILE *fp = NULL;
	uint32_t i;
	BASE_LOG_ERROR("null inputn------------------");
	if (!req_iov || !usr_context){
		BASE_LOG_ERROR("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);

	BASE_LOG_NOTICE("sync------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);
	BASE_LOG_NOTICE("------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);
	fp = fopen(file_path, "ab");
	if (!fp){
		BASE_LOG_ERROR("fopen path:%s fail.\n", file_path);
		return 0;
	}
	for(i = 0; i < req_iov->vec_num; i++){
		fwrite(req_iov->vec[i].data, 1, req_iov->vec[i].len, fp);
		fseek(fp, 0, SEEK_END);
	}
	fclose(fp);
	return 0;
}

static int process_oneway_async(const struct arpc_vmsg *req_iov, uint32_t *flags, void* usr_context)
{
	FILE *fp = NULL;
	char file_path[512] = {0};
	uint32_t i;
	int ret;
	struct arpc_vmsg *rsp_data;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;

	if (!req_iov || !usr_context){
		BASE_LOG_ERROR("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);
	BASE_LOG_NOTICE("async------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);
	BASE_LOG_NOTICE("------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);
	fp = fopen(file_path, "ab");
	if (!fp){
		BASE_LOG_ERROR("fopen path:%s fail.\n", file_path);
		return 0;
	}
	sha256_init(&ctx);
	for(i = 0; i < req_iov->vec_num; i++){
		fwrite(req_iov->vec[i].data, 1, req_iov->vec[i].len, fp);
		fseek(fp, 0, SEEK_END);
		sha256_update(&ctx, req_iov->vec[i].data, req_iov->vec[i].len);
	}
	sha256_final(&ctx, buf);
	fclose(fp);

	conver_hex_to_str(buf, sizeof(buf), buf_str, sizeof(buf_str));
	BASE_LOG_ERROR("sha256:%s.", (char*)buf_str);

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

	//ret = arpc_send_oneway_msg(session_fd, rsp_data, release_rsp, NULL);
	//if(ret){
	//	BASE_LOG_ERROR("arpc_send_oneway_msg:%s fail.", (char*)buf_str);
	//	release_rsp(rsp_data, NULL);
	//}
	return 0;
}

static struct arpc_session_ops ops ={
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

/*---------------------------------------------------------------------------*/
/* main									     */
/*---------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	uint32_t				i = 0;
	int 					ret = 0;
	uint64_t				offset = 0,send_len = 0, file_len =0;

	struct arpc_client_session_param param;
	struct arpc_msg *requst =NULL;
	arpc_session_handle_t session_fd;
	struct arpc_msg_param p;
	char *file_path = NULL;
	FILE *fp = NULL;
	BYTE buf[SHA256_BLOCK_SIZE];
	BYTE buf_str[SHA256_BLOCK_SIZE*2 +1];
	SHA256_CTX ctx;

	if (argc < 5) {
		printf("Usage: %s <host> <port> <file path> <req data>. \n", argv[0]);
		return 0;
	}
	printf("input:<%s> <%s> <%s> <%s>\n", argv[1], argv[2], argv[3], argv[4]);
	file_path = argv[3];
	fp = fopen(file_path, "rb");
    if(!fp) { 
         printf("can not open this file[%s],or not exist!\n", file_path);
         return 0; 
    }
	fseek(fp, 0, SEEK_END);
	file_len = ftell(fp);
	rewind(fp);
	printf("-----file_size:%lu\n", file_len);
	arpc_init();
	// 创建session
	param.con.type = ARPC_E_TRANS_TCP;
	memcpy(param.con.ipv4.ip, argv[1], IPV4_MAX_LEN);
	param.con.ipv4.port = atoi(argv[2]);
	param.req_data = argv[4];
	param.req_data_len = strlen(argv[4]);
	param.con_num = 4;
	param.rx_con_num = 2;
	param.ops = &ops;
	session_fd = arpc_client_create_session(&param);
	if (!session_fd){
		printf("arpc_client_create_session fail\n");
		goto end;
	}

	// 新建消息
	requst = arpc_new_msg(NULL);

	while(offset < file_len){
		sha256_init(&ctx);
		send_len = ((file_len - offset) > DATA_DEFAULT_MAX_LEN)? DATA_DEFAULT_MAX_LEN: (file_len - offset);
		printf("\n_____send_len:%lu, left_size:%lu____________\n", send_len, (file_len - offset));
		requst->send.head_len = strlen(file_path);
		requst->send.head = file_path;
		requst->send.total_data = send_len;
		requst->send.vec_num = (requst->send.total_data / BUF_MAX_SIZE) + 1;
		requst->proc_rsp_cb = NULL;

		// 读取文件
		requst->send.vec = malloc(requst->send.vec_num * sizeof(struct arpc_iov));
		for (i = 0; i  < requst->send.vec_num -1; i++) {
			fseek(fp, i*BUF_MAX_SIZE + offset, SEEK_SET);
			requst->send.vec[i].data = malloc(BUF_MAX_SIZE);
			requst->send.vec[i].len = fread(requst->send.vec[i].data, 1, BUF_MAX_SIZE, fp);
			if (requst->send.vec[i].len < BUF_MAX_SIZE){
				if(feof(fp)){
					break;
				}
			}
			sha256_update(&ctx, requst->send.vec[i].data, requst->send.vec[i].len);
		}
		fseek(fp, i*BUF_MAX_SIZE + offset, SEEK_SET);
		offset += send_len;
		send_len = send_len % BUF_MAX_SIZE;
		requst->send.vec[i].data = malloc(send_len);
		requst->send.vec[i].len = fread(requst->send.vec[i].data, 1, send_len, fp);
		if (requst->send.vec[i].len < send_len){
			printf("fread len fail\n");
		}
		sha256_update(&ctx, requst->send.vec[i].data, requst->send.vec[i].len);
		sha256_final(&ctx, buf);

		conver_hex_to_str(buf, sizeof(buf), buf_str, sizeof(buf_str));
		printf("send data sha256:%s.\n", (char*)buf_str);

		ret = arpc_do_request(session_fd, requst, -1);
		//ret = arpc_send_oneway_msg(session_fd, &requst->send, NULL, NULL);
		//usleep(500*1000);
		if (ret != 0){
			printf("arpc_do_request fail\n");
		}

		// 释放资源
		for (i = 0; i < requst->send.vec_num; i++) {
			if (requst->send.vec[i].data){
				free(requst->send.vec[i].data);
			}
		}
		printf("receive sha256:%s.\n", (char*)requst->receive.head);
		for (i = 0; i < requst->receive.vec_num; i++) {
			if (requst->receive.vec[i].data){
				printf("receive iov[%u]:%s\n", i, (char*)requst->receive.vec[i].data);
			}
		}

		free(requst->send.vec);
		requst->send.vec = NULL;
		arpc_reset_msg(requst);
	}
	sleep(5);
	arpc_delete_msg(&requst);
	arpc_client_destroy_session(&session_fd);
	printf("file send complete:%s.\n\n", file_path);
end:
	if (fp)
		fclose(fp);
	fp =NULL;
	arpc_finish();
	return 0;
}

