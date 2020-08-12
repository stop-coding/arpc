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

#include "arpc_api.h"

static char rsp_header[] = "<file:rsp>";

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
	SET_METHOD(*flag, METHOD_ALLOC_DATA_BUF);
	return 0;
}

static int process_rx_data(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void *usr_context)
{
	char file_path[512] = {0};
	FILE *fp = NULL;
	uint32_t i;
	
	if (!req_iov || !usr_context){
		printf("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);

	printf("------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);

	fp = fopen(file_path, "ab");
	if (!fp){
		printf("fopen path:%s fail.\n", file_path);
		return 0;
	}
	for(i = 0; i < req_iov->vec_num; i++){
		fwrite(req_iov->vec[i].data, 1, req_iov->vec[i].len, fp);
		fseek(fp, 0, SEEK_END);
	}
	fclose(fp);
	return 0;
}
static int release_rsp(struct arpc_vmsg *rsp_iov, void *usr_context)
{
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
			printf("fopen path:%s fail.\n", file_path);
			return 0;
		}
		fclose(fp);
	}
	return 0;
}

int new_session_end(arpc_session_handle_t fd, struct arpc_new_session_rsp *param, void* usr_context)
{
	return 0;
}


static int process_async(const struct arpc_vmsg *req_iov, struct arpc_rsp *rsp, void* usr_context)
{
	FILE *fp = NULL;
	char file_path[512] = {0};
	uint32_t i;
	if (!req_iov || !usr_context){
		printf("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);

	printf("----dddd--file:%s, receive len:%lu.\n", file_path, req_iov->total_data);

	fp = fopen(file_path, "ab");
	if (!fp){
		printf("fopen path:%s fail.\n", file_path);
		return 0;
	}
	for(i = 0; i < req_iov->vec_num; i++){
		fwrite(req_iov->vec[i].data, 1, req_iov->vec[i].len, fp);
		fseek(fp, 0, SEEK_END);
	}
	fclose(fp);
	sleep(3);
	printf("----dddd--end.\n");
	return 0;
}
static int process_rx_oneway_data(const struct arpc_vmsg *req_iov, void *usr_context)
{
	char file_path[512] = {0};
	FILE *fp = NULL;
	uint32_t i;

	if (!req_iov || !usr_context){
		printf("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);

	printf("------file:%s, receive len:%lu.\n", file_path, req_iov->total_data);

	fp = fopen(file_path, "ab");
	if (!fp){
		printf("fopen path:%s fail.\n", file_path);
		return 0;
	}
	for(i = 0; i < req_iov->vec_num; i++){
		fwrite(req_iov->vec[i].data, 1, req_iov->vec[i].len, fp);
		fseek(fp, 0, SEEK_END);
	}
	fclose(fp);
	return 0;
}

static int process_oneway_async(const struct arpc_vmsg *req_iov, void* usr_context)
{
	FILE *fp = NULL;
	char file_path[512] = {0};
	uint32_t i;
	if (!req_iov || !usr_context){
		printf("null inputn");
		return 0;
	}
	sprintf(file_path, "./rev_%s", (char *)usr_context);

	fp = fopen(file_path, "ab");
	if (!fp){
		printf("fopen path:%s fail.\n", file_path);
		return 0;
	}
	for(i = 0; i < req_iov->vec_num; i++){
		fwrite(req_iov->vec[i].data, 1, req_iov->vec[i].len, fp);
		fseek(fp, 0, SEEK_END);
	}
	fclose(fp);

	return 0;
}
/*---------------------------------------------------------------------------*/
/* main									     */
/*---------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
	struct arpc_server_param param;
	arpc_session_handle_t fd = NULL;
	char file_name[256] = {0};
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

	if (argc < 2) {
		printf("Usage: %s <host> <port>n", argv[0]);
		return 0;
	}

	arpc_init();
	memset(&param, 0, sizeof(param));
	param.con.type = ARPC_E_TRANS_TCP;
	memcpy(param.con.ipv4.ip, argv[1], IPV4_MAX_LEN);
	param.con.ipv4.port = atoi(argv[2]);

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

