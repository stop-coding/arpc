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

#ifndef _ARPC_PROTO_H
#define _ARPC_PROTO_H

#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PACKED_MEMORY(__declaration__) \
		__declaration__ __attribute__((__packed__))

#define ntohll(x) (((uint64_t)(ntohl((int)((x << 32) >> 32))) << 32) | \
		    (unsigned int)ntohl(((int)(x >> 32))))
#define htonll(x) ntohll(x)

#define ARPC_TVL_MAGIC  (0xfa378)

PACKED_MEMORY(struct arpc_tlv {
	uint32_t		magic;
	uint32_t		type;
	uint64_t		len;
});

inline static int32_t arpc_write_tlv(uint32_t type, uint64_t len, uint8_t *buffer)
{
	struct arpc_tlv *tlv = (struct arpc_tlv *)buffer;

	tlv->magic	= htonl(ARPC_TVL_MAGIC);
	tlv->type	= htonl(type);
	tlv->len	= htonll(len);

	return sizeof(struct arpc_tlv) + (int32_t)len;
}

inline static int32_t arpc_read_tlv(uint32_t *type, uint64_t *len, void **value,
		    uint8_t *buffer)
{
	struct arpc_tlv *tlv;

	tlv = (struct arpc_tlv *)buffer;
	if (unlikely(tlv->magic != htonl(ARPC_TVL_MAGIC)))
		return -1;

	*type	= ntohl(tlv->type);
	*len	= ntohll(tlv->len);
	*value =  buffer + sizeof(struct arpc_tlv);

	return sizeof(struct arpc_tlv) + (int32_t)*len;
}

typedef enum arpc_proto{
	ARPC_PROTO_NEW_SESSION 		 = 10026,
	ARPC_PROTO_NEW_SESSION_PRIVATE,
	ARPC_PROTO_MSG_INTER_HEAD,
	ARPC_PROTO_MSG_USER_HEAD,
}arpc_proto_t;

PACKED_MEMORY(struct arpc_proto_new_session
{
	uint32_t max_head_len;
	uint64_t max_data_len;
	uint32_t max_iov_len;
});

int32_t pack_new_session(const struct arpc_proto_new_session *proto, uint8_t *buffer, uint32_t bufflen);
int32_t unpack_new_session(const uint8_t *buffer, const uint32_t bufflen, struct arpc_proto_new_session *proto);

PACKED_MEMORY(struct arpc_msg_attr
{
	uint64_t req_crc;
	uint64_t rsp_crc;
});

int32_t pack_msg_attr(const struct arpc_msg_attr *proto, uint8_t *buffer, uint32_t bufflen);
int32_t unpack_msg_attr(const uint8_t *buffer, const uint32_t bufflen, struct arpc_msg_attr *proto);

#ifdef __cplusplus
}
#endif

#endif /*XIO_API_H */
