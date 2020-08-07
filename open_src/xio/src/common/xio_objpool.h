/*
 * Copyright (c) 2013 Mellanox Technologies®. All rights reserved.
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the Mellanox Technologies® BSD license
 * below:
 *
 *      - Redistribution and use in source and binary forms, with or without
 *        modification, are permitted provided that the following conditions
 *        are met:
 *
 *      - Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *      - Neither the name of the Mellanox Technologies® nor the names of its
 *        contributors may be used to endorse or promote products derived from
 *        this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef XIO_OBJPOOL_H
#define XIO_OBJPOOL_H

/*---------------------------------------------------------------------------*/
/* opaque data type							     */
/*---------------------------------------------------------------------------*/
struct xio_objpool;

/**
 * create dynamically growing objects pool
 *
 * @param[in] size		size of the object managed by the pool
 * @param[in] init_nr		initial number of objects to allocate
 * @param[in] grow_nr		growing number of objects each time the pool
 *				is empty.
 *
 * @return pointer to object pool
 */
struct xio_objpool	*xio_objpool_create(int size, int init_nr, int grow_nr);

/**
 * destroy objects pool
 *
 * @param[in] opool		pointer to objects pool to be destroyed
 *
 */
void			xio_objpool_destroy(struct xio_objpool *opool);

/**
 * allocate object from the pool
 *
 * @param[in] opool		pointer to objects pool
 *
 * @return pointer to single object from the pool
 */
void			*xio_objpool_alloc(struct xio_objpool *opool);

/**
 * free object back to the pool
 *
 * @param[in] obj		pointer to object
 *
 */
void			xio_objpool_free(void *obj);

#endif	/* XIO_OBJPOOL_H */

