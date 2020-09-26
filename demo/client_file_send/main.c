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

		//ret = arpc_do_request(session_fd, requst, -1);
		ret = arpc_send_oneway_msg(session_fd, &requst->send, NULL, NULL);
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

