/*
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Author: Michael S. Tsirkin <mst@mellanox.co.il>
 */

#ifndef GET_CLOCK_H
#define GET_CLOCK_H

#if defined(__x86_64__) || defined(__i386__)

/* Note: only x86 CPUs which have rdtsc instruction are supported. */
typedef unsigned long long cycles_t;
static inline cycles_t get_cycles(void)
{
	union {
		cycles_t val;
		struct {
			unsigned int low;
			unsigned int high;
		} __attribute__((packed));
	} value;

	asm volatile ("rdtsc" : "=a" (value.low), "=d" (value.high));
	return value.val;
}
#elif defined(__PPC__) || defined(__PPC64__)
/* Note: only PPC CPUs which have mftb instruction are supported. */
/* PPC64 has mftb */
typedef unsigned long cycles_t;
static inline cycles_t get_cycles(void)
{
	cycles_t ret;

	asm volatile ("mftb %0" : "=r" (ret) : );
	return ret;
}
#elif defined(__ia64__)
/* Itanium2 and up has ar.itc (Itanium1 has errata) */
typedef unsigned long cycles_t;
static inline cycles_t get_cycles(void)
{
	cycles_t ret;

	asm volatile ("mov %0=ar.itc" : "=r" (ret));
	return ret;
}
#elif defined(WIN32)
#include <Windows.h>

typedef LONGLONG cycles_t;
static __inline cycles_t get_cycles(void)
{
	LARGE_INTEGER performance_count;

	return QueryPerformanceCounter(&performance_count) ?
				       performance_count.QuadPart : 0;
}

#else
#warning get_cycles not implemented for this architecture: attempt asm/timex.h
#include <linux/timex.h>
#endif

extern double get_cpu_mhz(int);

#endif
