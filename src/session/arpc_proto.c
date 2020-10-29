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

#include <stdio.h>
#include <string.h>

#include "base_log.h"
#include "arpc_com.h"



/**
 * @brief Place an unsigned byte into the buffer
 *
 * @param b the byte to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_write_uint8(uint8_t b, int bindex, uint8_t *buffer)
{
	*(buffer + bindex) = b;
	return sizeof(b);
}

/**
 * @brief Get an unsigned byte from the buffer
 *
 * @param b the byte to get
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_read_uint8(uint8_t *b, int bindex,
				    const uint8_t *buffer)
{
	*b = *(buffer + bindex);
	return sizeof(*b);
}

/**
 * @brief Place a signed byte into the buffer
 *
 * @param b the byte to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_write_int8(int8_t b, int bindex, uint8_t *buffer)
{
	*(buffer + bindex) = (uint8_t)b;
	return sizeof(b);
}

/**
 * @brief Get a signed byte from the buffer
 *
 * @param b the byte to get
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_read_int8(int8_t *b, int bindex,
				   const uint8_t *buffer)
{
	*b = (int8_t)*(buffer + bindex);
	return sizeof(*b);
}

/**
 * @brief Place two unsigned bytes into the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_write_uint16(uint16_t b, const int bindex,
				      uint8_t *buffer)
{
	buffer[bindex]   = (b >> 8) & 0xff;
	buffer[bindex+1] = (b)	& 0xff;

	return sizeof(b);
}

/**
 * @brief Get two unsigned bytes from the buffer
 *
 * @param b the bytes to get
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_read_uint16(uint16_t *b, const int bindex,
				     const uint8_t *buffer)
{
	*b = ((((uint32_t)buffer[bindex]) << 8)
			|  ((uint32_t)buffer[bindex+1]));

	return sizeof(*b);
}

/**
 * @brief Place two signed bytes into the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_write_int16(int16_t b, int bindex, uint8_t *buffer)
{
	return arpc_write_uint16(b, bindex, buffer);
}

/**
 * @brief Get two signed bytes from the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_read_int16(int16_t *b, int bindex,
				    const uint8_t *buffer)
{
	return arpc_read_uint16((uint16_t *)b, bindex, buffer);
}

/**
 * @brief Place four unsigned bytes into the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_write_uint32(uint32_t b, const int bindex,
				      uint8_t *buffer)
{
	buffer[bindex]   = (b >> 24) & 0xff;
	buffer[bindex+1] = (b >> 16) & 0xff;
	buffer[bindex+2] = (b >> 8)  & 0xff;
	buffer[bindex+3] = (b)	 & 0xff;
	return sizeof(b);
}

/**
 * @brief Get four unsigned bytes from the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_read_uint32(uint32_t *b, const int bindex,
				     const uint8_t *buffer)
{
	*b = (uint32_t)(buffer[bindex]) << 24 |
	     (uint32_t)(buffer[bindex+1]) << 16 |
	     (uint32_t)(buffer[bindex+2]) << 8 |
	     (uint32_t)(buffer[bindex+3]);

	return sizeof(*b);
}

/**
 * @brief Place four signed bytes into the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_write_int32(int32_t b, int bindex, uint8_t *buffer)
{
	buffer[bindex]   = (b >> 24) & 0xff;
	buffer[bindex+1] = (b >> 16) & 0xff;
	buffer[bindex+2] = (b >> 8)  & 0xff;
	buffer[bindex+3] = (b)	 & 0xff;
	return sizeof(b);
}

/**
 * @brief Get four signed bytes from the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_read_int32(int32_t *b, int bindex,
				    const uint8_t *buffer)
{
	*b = ((((uint32_t)buffer[bindex])   << 24) |
	      (((uint32_t)buffer[bindex+1]) << 16) |
	      (((uint32_t)buffer[bindex+2]) << 8)  |
	      ((uint32_t)buffer[bindex+3]));

	return sizeof(*b);
}

/**
 * @brief Place four unsigned bytes form the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_write_uint64(uint64_t b, const int bindex,
				      uint8_t *buffer)
{
	buffer[bindex]   = (b >> 56) & 0xff;
	buffer[bindex+1] = (b >> 48) & 0xff;
	buffer[bindex+2] = (b >> 40) & 0xff;
	buffer[bindex+3] = (b >> 32) & 0xff;
	buffer[bindex+4] = (b >> 24) & 0xff;
	buffer[bindex+5] = (b >> 16) & 0xff;
	buffer[bindex+6] = (b >> 8)  & 0xff;
	buffer[bindex+7] = (b)	 & 0xff;
	return sizeof(b);
}

/**
 * @brief Get four unsigned bytes from the buffer
 *
 * @param b the bytes to get
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_read_uint64(uint64_t *b, const int bindex,
				     const uint8_t *buffer)
{
	*b = ((((uint64_t)buffer[bindex])   << 56)
			| (((uint64_t)buffer[bindex+1]) << 48)
			| (((uint64_t)buffer[bindex+2]) << 40)
			| (((uint64_t)buffer[bindex+3]) << 32)
			| (((uint64_t)buffer[bindex+4]) << 24)
			| (((uint64_t)buffer[bindex+5]) << 16)
			| (((uint64_t)buffer[bindex+6]) << 8)
			|  ((uint64_t)buffer[bindex+7]));

	return sizeof(*b);
}

/**
 * @brief Place four signed bytes into the buffer
 *
 * @param b the bytes to add
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_write_int64(int64_t b, int bindex, uint8_t *buffer)
{
	return arpc_write_uint64(b, bindex, buffer);
}

/**
 * @brief Get four signed bytes from the buffer
 *
 * @param b the bytes to get
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline size_t arpc_read_int64(int64_t *b, int bindex,
				    const uint8_t *buffer)
{
	return arpc_read_uint64((uint64_t *)b, bindex, buffer);
}


//确保顺序写
int32_t pack_new_session(const struct arpc_proto_new_session *proto, uint8_t *buffer, uint32_t bufflen)
{
	int32_t index = 0;
	LOG_THEN_RETURN_VAL_IF_TRUE(bufflen < sizeof(struct arpc_proto_new_session), ARPC_ERROR, "buf invalid.");
	index += arpc_write_uint32(proto->max_head_len, index, buffer);
	index += arpc_write_uint64(proto->max_data_len, index, buffer);
	index += arpc_write_uint32(proto->max_iov_len, index, buffer);
	return index;
}
int32_t unpack_new_session(const uint8_t *buffer, const uint32_t bufflen, struct arpc_proto_new_session *proto)
{
	int32_t index = 0;
	LOG_THEN_RETURN_VAL_IF_TRUE(bufflen < sizeof(struct arpc_proto_new_session), ARPC_ERROR, "struct error.");
	index += arpc_read_uint32(&proto->max_head_len, index, buffer);
	index += arpc_read_uint64(&proto->max_data_len, index, buffer);
	index += arpc_read_uint32(&proto->max_iov_len, index, buffer);
	return index;
}
int32_t pack_msg_attr(const struct arpc_msg_attr *proto, uint8_t *buffer, uint32_t bufflen)
{
	int32_t index = 0;
	LOG_THEN_RETURN_VAL_IF_TRUE(bufflen < sizeof(struct arpc_msg_attr), ARPC_ERROR, "buf invalid.");
	index += arpc_write_uint64(proto->req_crc, index, buffer);
	index += arpc_write_uint64(proto->rsp_crc, index, buffer);
	return index;
}
int32_t unpack_msg_attr(const uint8_t *buffer, const uint32_t bufflen, struct arpc_msg_attr *proto)
{
	int32_t index = 0;
	LOG_THEN_RETURN_VAL_IF_TRUE(bufflen < sizeof(struct arpc_msg_attr), ARPC_ERROR, "struct error.");
	index += arpc_read_uint64(&proto->req_crc, index, buffer);
	index += arpc_read_uint64(&proto->rsp_crc, index, buffer);
	return index;
}

