/*
 * Copyright (c) 2016 Hisilicon Limited.
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
 */

#include <errno.h>
#include <sys/mman.h>
#include <util/util.h>

#include "hns_roce_u.h"
#include "hns_roce_u_ai.h"

static unsigned int g_bitmap_list[BITMAP_WORD_NUM] = { 0 };
static struct hns_roce_mem_pool *g_mem_pool_list[BITMAP_WORD_SIZE] = { NULL };

static int bitmap_alloc(unsigned int *bitmap_list, unsigned int list_size, unsigned int *index)
{
	unsigned int i;
	unsigned int j;

	for (i = 0; i < list_size; i++) {
		if (bitmap_list[i] != 0xFFFFFFFF) {
			break;
		}
	}

	if (i == list_size) {
		roce_err("alloc bitmap failed, has no empty bit");
		return -EINVAL;
	}

	for (j = 0; j < BITMAP_WORD_LEN; j++) {
		if (((bitmap_list[i] >> j) & 0x1) == 0) {
			break;
		}
	}

	bitmap_list[i] = bitmap_list[i] | (0x1 << j);

	*index = i * BITMAP_WORD_LEN + j;
	return 0;
}

static inline void bitmap_free(unsigned int *bitmap_list, unsigned int list_size, unsigned int index)
{
	unsigned int word_index = index / BITMAP_WORD_LEN;
	unsigned int bit_index = index % BITMAP_WORD_LEN;

	if (word_index >= list_size) {
		roce_err("free bitmap failed, word_index[%u] >= list_size[%u]", word_index, list_size);
		return;
	}

	bitmap_list[word_index] = bitmap_list[word_index] & (~(1 << bit_index));
}

static void hns_roce_hal_calc_mem_size(const struct roce_mem_cq_qp_attr *mem_attr, unsigned int *size)
{
#define WQE_SHIFT_START         6
#define HNS_ROCE_CQE_ENTRY_SIZE 0x20
#define SEND_RECV_DB_PAGE_NUM   2
	unsigned int cq_buf_db_size = 0;
	unsigned int qp_buf_db_size = 0;
	unsigned int sq_sge_shift = 0;
	unsigned int sq_wqe_shift = 0;
	unsigned int rq_wqe_shift = 0;
	unsigned int sq_wqe_size = 0;

	// calc qp buf, send, recv db size
	sq_wqe_size = HNS_ROCE_AI_SQ_WQE_SIZE;
	for (sq_wqe_shift = WQE_SHIFT_START; (1U << sq_wqe_shift) < sq_wqe_size;) {
		sq_wqe_shift++;
	}
	if (mem_attr->send_sge_num > HNS_ROCE_SGE_IN_WQE) {
		sq_sge_shift = HNS_ROCE_SGE_SHIFT;
	}
	for (rq_wqe_shift = HNS_ROCE_SGE_SHIFT;
		(1U << rq_wqe_shift) < (HNS_ROCE_SGE_SIZE * mem_attr->recv_sge_num);) {
		rq_wqe_shift++;
	}
	qp_buf_db_size = align((unsigned int)(mem_attr->send_qp_depth << sq_wqe_shift), PAGE_ALIGN_4KB) +
			 align((unsigned int)(mem_attr->send_sge_num << sq_sge_shift), PAGE_ALIGN_4KB) +
			 align((unsigned int)(mem_attr->recv_qp_depth << rq_wqe_shift), PAGE_ALIGN_4KB) +
			 SEND_RECV_DB_PAGE_NUM * PAGE_ALIGN_4KB;

	// calc send, recv cq buf and db size
	cq_buf_db_size = align((unsigned int)(mem_attr->send_cq_depth * HNS_ROCE_CQE_ENTRY_SIZE), PAGE_ALIGN_4KB) +
			 align((unsigned int)(mem_attr->recv_cq_depth * HNS_ROCE_CQE_ENTRY_SIZE), PAGE_ALIGN_4KB) +
			 SEND_RECV_DB_PAGE_NUM * PAGE_ALIGN_4KB;

	// total size
	*size = align((unsigned int)(qp_buf_db_size + cq_buf_db_size), PAGE_ALIGN_2MB);
	return;
}

int roce_init_mem_pool(const struct roce_mem_cq_qp_attr *mem_attr, struct rdma_lite_device_mem_attr *mem_data,
    unsigned int dev_id)
{
	struct hns_roce_mem_pool *mem_pool = NULL;
	int ret = 0;

	if (mem_attr == NULL || mem_data == NULL) {
		roce_err("hns init pool param error, mem_attr[%pK] or mem_data[%pK]",
			mem_attr, mem_data);
		return -EINVAL;
	}
	if (mem_attr->mem_align != LITE_ALIGN_2MB) {
		roce_err("hns init pool param error, mem_align[0x%x] is not 0x%x", mem_attr->mem_align, LITE_ALIGN_2MB);
		return -EINVAL;
	}

	hns_roce_hal_calc_mem_size(mem_attr, &mem_data->mem_size);
	if (mem_data->mem_size == 0) {
		roce_err("hns init pool failed, calc mem size is 0");
		return -EINVAL;
	}

	ret = bitmap_alloc(g_bitmap_list, BITMAP_WORD_NUM, &mem_data->mem_idx);
	if (ret != 0) {
		roce_err("hns init pool failed, bitmap_alloc failed ret = %d", ret);
		return ret;
	}

	mem_pool = (struct hns_roce_mem_pool *)calloc(1, sizeof(struct hns_roce_mem_pool));
	if (mem_pool == NULL) {
		roce_err("hns init pool failed, alloc mem_pool failed");
		ret = -ENOMEM;
		goto err_calloc;
	}
	mem_pool->roce_buf.mem_align = mem_attr->mem_align;
	mem_pool->roce_buf.mem_idx = mem_data->mem_idx;
	ret = hns_roce_lite_alloc_buf(&mem_pool->roce_buf.buf, &mem_pool->roce_buf.length, mem_data->mem_size,
		PAGE_ALIGN_2MB, dev_id);
	if (ret != 0) {
		roce_err("hns init pool failed, hal_alloc_buf failed ret = %d", ret);
		goto err_alloc_buf;
	}

	mem_pool->roce_buf.offset = 0;
	mem_pool->used_size = 0;
	g_mem_pool_list[mem_data->mem_idx] = mem_pool;
	mem_data->va = (unsigned long long)mem_pool->roce_buf.buf;

	return 0;

err_alloc_buf:
	free(mem_pool);
err_calloc:
	bitmap_free(g_bitmap_list, BITMAP_WORD_NUM, mem_data->mem_idx);

	return ret;
}

int hns_roce_hal_alloc_mem(struct hns_roce_buf *buf, unsigned int size, unsigned int page_size)
{
	struct hns_roce_mem_pool *mem_pool = NULL;
	unsigned int alloc_size = 0;

	if (buf->mem_align != LITE_ALIGN_2MB || buf->mem_idx >= BITMAP_WORD_SIZE) {
		roce_err("hns alloc mem param error, mem_align[0x%x] is not 0x2 or mem_idx[%u] >= %u",
			buf->mem_align, buf->mem_idx, BITMAP_WORD_SIZE);
		return -EINVAL;
	}

	alloc_size = (unsigned int)align(size, page_size);
	mem_pool = g_mem_pool_list[buf->mem_idx];
	if (mem_pool == NULL) {
		roce_err("hns alloc pool mem err, mem_idx:%u, mem_pool is NULL", buf->mem_idx);
		return -EINVAL;
	}
	if (mem_pool->roce_buf.mem_idx != buf->mem_idx) {
		roce_err("hns alloc pool mem err, mem_idx:%u inconsistent with pool mem_idx:0x%x",
			buf->mem_idx, mem_pool->roce_buf.mem_idx);
		return -EINVAL;
	}

	if (mem_pool->roce_buf.offset + alloc_size > mem_pool->roce_buf.length) {
		roce_err("hns alloc mem pool mem exhaust");
		return -ENOMEM;
	}

	// prepare out buf
	buf->buf = mem_pool->roce_buf.buf;
	buf->length = alloc_size;
	buf->offset = mem_pool->roce_buf.offset;

	// update mem pool usage
	mem_pool->roce_buf.offset += alloc_size;
	mem_pool->used_size += alloc_size;

	return 0;
}

int hns_roce_hal_free_mem(struct hns_roce_buf *buf)
{
	struct hns_roce_mem_pool *mem_pool = NULL;

	if (buf->mem_align != LITE_ALIGN_2MB || buf->mem_idx >= BITMAP_WORD_SIZE) {
		roce_err("hns free pool param error, mem_align[0x%x] is not 0x2 or mem_idx[%u] >= %u",
			buf->mem_align, buf->mem_idx, BITMAP_WORD_SIZE);
		return -EINVAL;
	}

	mem_pool = g_mem_pool_list[buf->mem_idx];
	if (mem_pool == NULL) {
		roce_err("hns free pool mem err, mem_idx:%u, mem_pool is NULL", buf->mem_idx);
		return -EINVAL;
	}
	if (mem_pool->roce_buf.buf != buf->buf) {
		roce_err("hns free pool mem addr is err, mem_idx:%u", buf->mem_idx);
		return -EINVAL;
	}

	if (mem_pool->used_size < buf->length) {
		roce_err("hns free pool len is err, mem_pool->used_size:%u, buf->length:%u",
			mem_pool->used_size, buf->length);
		return -EINVAL;
	}

	mem_pool->used_size -= buf->length;
	return (int)(mem_pool->used_size);
}

int roce_deinit_mem_pool(unsigned int mem_idx)
{
	struct hns_roce_mem_pool *mem_pool = NULL;
	int ret = 0;

	if (mem_idx >= BITMAP_WORD_SIZE) {
		roce_err("hns deinit pool param error, mem_idx[%u] >= %u",
			mem_idx, BITMAP_WORD_SIZE);
		return -EINVAL;
	}

	mem_pool = g_mem_pool_list[mem_idx];
	if (mem_pool == NULL) {
		roce_err("hns deinit pool mem err, mem_idx:%u, mem_pool is NULL", mem_idx);
		return -EINVAL;
	}

	if (mem_pool->used_size > 0) {
		roce_err("hns deinit pool failed, still has used_size:%u", mem_pool->used_size);
		return -EINVAL;
	}

	// should free & munmap buf
	ibv_dofork_range(mem_pool->roce_buf.buf, mem_pool->roce_buf.length);

	if (munmap(mem_pool->roce_buf.buf, mem_pool->roce_buf.length)) {
		roce_err("free buf munmap error, mem_pool->roce_buf.length:%d", mem_pool->roce_buf.length);
		ret = -ENOMEM;
	}
	mem_pool->roce_buf.buf = NULL;

	free(mem_pool);
	mem_pool = NULL;
	g_mem_pool_list[mem_idx] = NULL;
	bitmap_free(g_bitmap_list, BITMAP_WORD_NUM, mem_idx);
	return ret;
}

int hns_roce_alloc_buf(struct hns_roce_buf *buf, unsigned int size,
		       int page_size, unsigned int dev_id, enum hns_roce_buf_type buf_type)
{
	int ret;

	if (buf_type == HNS_ROCE_BUF_TYPE_LITE_ALIGN_4KB) {
		buf->offset = 0;
		return hns_roce_lite_alloc_buf(&buf->buf, &buf->length, size, page_size, dev_id);
	} else if (buf_type == HNS_ROCE_BUF_TYPE_LITE_ALIGN_2MB) {
		return hns_roce_hal_alloc_mem(buf, align(size, page_size), page_size);
	} else if (buf_type == HNS_ROCE_BUF_TYPE_AI_ALIGN_4KB) {
		buf->offset = 0;
		return hns_roce_ai_alloc_buf(&buf->buf, &buf->length, size, page_size, dev_id,
			buf->ai_op_support, buf->grp_id);
	} else {
		buf->length = align(size, page_size);
		buf->buf = mmap(NULL, buf->length, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		buf->offset = 0;
		if (buf->buf == MAP_FAILED)
			return errno;
	}

	ret = ibv_dontfork_range(buf->buf, buf->length);
	if (ret)
		munmap(buf->buf, buf->length);

	return ret;
}

void hns_roce_free_buf(struct hns_roce_buf *buf)
{
	int ret;

	if (!buf->buf)
		return;

	if (buf->mem_align == LITE_ALIGN_2MB) {
		ret = hns_roce_hal_free_mem(buf);
		if (ret < 0) {
			roce_err("free buf mem error, ret:%d", ret);
		}
		buf->buf = NULL;
		return;
	}

	if (buf->ai_op_support != 0) {
		hns_roce_ai_free_buf(buf->ai_op_support, buf->buf);
		return;
	}

	ibv_dofork_range(buf->buf, buf->length);

	munmap(buf->buf, buf->length);
}

struct hns_roce_dca_mem {
	uint32_t handle;
	struct list_node entry;
	struct hns_roce_buf buf;
	struct hns_roce_context *ctx;
};

static void free_dca_mem(struct hns_roce_context *ctx,
			 struct hns_roce_dca_mem *mem)
{
	hns_roce_free_buf(&mem->buf);
	free(mem);
}

static struct hns_roce_dca_mem *alloc_dca_mem(uint32_t size)
{
	struct hns_roce_dca_mem *mem = NULL;
	int ret;

	mem = malloc(sizeof(struct hns_roce_dca_mem));
	if (!mem) {
		errno = ENOMEM;
		return NULL;
	}

	ret = hns_roce_alloc_buf(&mem->buf, size, HNS_HW_PAGE_SIZE, 0, HNS_ROCE_BUF_TYPE_NORMAL);
	if (ret) {
		errno = ENOMEM;
		free(mem);
		return NULL;
	}

	return mem;
}

static inline uint64_t dca_mem_to_key(struct hns_roce_dca_mem *dca_mem)
{
	return (uintptr_t)dca_mem;
}

static struct hns_roce_dca_mem *key_to_dca_mem(struct hns_roce_dca_ctx *ctx,
					       uint64_t key)
{
	struct hns_roce_dca_mem *mem;
	struct hns_roce_dca_mem *tmp;

	list_for_each_safe(&ctx->mem_list, mem, tmp, entry) {
		if (dca_mem_to_key(mem) == key)
			return mem;
	}

	return NULL;
}

static inline void *dca_mem_addr(struct hns_roce_dca_mem *dca_mem, int offset)
{
	return dca_mem->buf.buf + offset;
}

static int register_dca_mem(struct hns_roce_context *ctx, uint64_t key,
			    void *addr, uint32_t size, uint32_t *handle)
{
	struct ib_uverbs_attr *attr;
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, HNS_IB_OBJECT_DCA_MEM,
			       HNS_IB_METHOD_DCA_MEM_REG, 4);
	fill_attr_in_uint32(cmd, HNS_IB_ATTR_DCA_MEM_REG_LEN, size);
	fill_attr_in_uint64(cmd, HNS_IB_ATTR_DCA_MEM_REG_ADDR,
			    ioctl_ptr_to_u64(addr));
	fill_attr_in_uint64(cmd, HNS_IB_ATTR_DCA_MEM_REG_KEY, key);
	attr = fill_attr_out_obj(cmd, HNS_IB_ATTR_DCA_MEM_REG_HANDLE);

	ret = execute_ioctl(&ctx->ibv_ctx.context, cmd);
	if (ret) {
		verbs_err(&ctx->ibv_ctx, "failed to reg DCA mem, ret = %d.\n",
			  ret);
		return ret;
	}

	*handle = read_attr_obj(HNS_IB_ATTR_DCA_MEM_REG_HANDLE, attr);

	return 0;
}

static void deregister_dca_mem(struct hns_roce_context *ctx, uint32_t handle)
{
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, HNS_IB_OBJECT_DCA_MEM,
			       HNS_IB_METHOD_DCA_MEM_DEREG, 1);
	fill_attr_in_obj(cmd, HNS_IB_ATTR_DCA_MEM_DEREG_HANDLE, handle);
	ret = execute_ioctl(&ctx->ibv_ctx.context, cmd);
	if (ret)
		verbs_warn(&ctx->ibv_ctx,
			   "failed to dereg DCA mem-%u, ret = %d.\n",
			   handle, ret);
}

void hns_roce_cleanup_dca_mem(struct hns_roce_context *ctx)
{
	struct hns_roce_dca_ctx *dca_ctx = &ctx->dca_ctx;
	struct hns_roce_dca_mem *mem;
	struct hns_roce_dca_mem *tmp;

	list_for_each_safe(&dca_ctx->mem_list, mem, tmp, entry)
		deregister_dca_mem(ctx, mem->handle);
}

struct hns_dca_mem_shrink_resp {
	uint32_t free_mems;
	uint64_t free_key;
};

static int shrink_dca_mem(struct hns_roce_context *ctx, uint32_t handle,
			  uint64_t size, struct hns_dca_mem_shrink_resp *resp)
{
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, HNS_IB_OBJECT_DCA_MEM,
			       HNS_IB_METHOD_DCA_MEM_SHRINK, 4);
	fill_attr_in_obj(cmd, HNS_IB_ATTR_DCA_MEM_SHRINK_HANDLE, handle);
	fill_attr_in_uint64(cmd, HNS_IB_ATTR_DCA_MEM_SHRINK_RESERVED_SIZE, size);
	fill_attr_out(cmd, HNS_IB_ATTR_DCA_MEM_SHRINK_OUT_FREE_KEY,
		      &resp->free_key, sizeof(resp->free_key));
	fill_attr_out(cmd, HNS_IB_ATTR_DCA_MEM_SHRINK_OUT_FREE_MEMS,
		      &resp->free_mems, sizeof(resp->free_mems));

	ret = execute_ioctl(&ctx->ibv_ctx.context, cmd);
	if (ret)
		verbs_err(&ctx->ibv_ctx, "failed to shrink DCA mem, ret = %d.\n",
			  ret);

	return ret;
}

struct hns_dca_mem_query_resp {
	uint64_t key;
	uint32_t offset;
	uint32_t page_count;
};

static int query_dca_mem(struct hns_roce_context *ctx, uint32_t handle,
			 uint32_t index, struct hns_dca_mem_query_resp *resp)
{
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, HNS_IB_OBJECT_DCA_MEM,
			       HNS_IB_METHOD_DCA_MEM_QUERY, 5);
	fill_attr_in_obj(cmd, HNS_IB_ATTR_DCA_MEM_QUERY_HANDLE, handle);
	fill_attr_in_uint32(cmd, HNS_IB_ATTR_DCA_MEM_QUERY_PAGE_INDEX, index);
	fill_attr_out(cmd, HNS_IB_ATTR_DCA_MEM_QUERY_OUT_KEY,
		      &resp->key, sizeof(resp->key));
	fill_attr_out(cmd, HNS_IB_ATTR_DCA_MEM_QUERY_OUT_OFFSET,
		      &resp->offset, sizeof(resp->offset));
	fill_attr_out(cmd, HNS_IB_ATTR_DCA_MEM_QUERY_OUT_PAGE_COUNT,
		      &resp->page_count, sizeof(resp->page_count));
	ret = execute_ioctl(&ctx->ibv_ctx.context, cmd);
	if (ret)
		verbs_err(&ctx->ibv_ctx,
			  "failed to query DCA mem-%u, ret = %d.\n",
			  handle, ret);

	return ret;
}

void hns_roce_detach_dca_mem(struct hns_roce_context *ctx, uint32_t handle,
			     struct hns_roce_dca_detach_attr *attr)
{
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, HNS_IB_OBJECT_DCA_MEM,
			       HNS_IB_METHOD_DCA_MEM_DETACH, 4);
	fill_attr_in_obj(cmd, HNS_IB_ATTR_DCA_MEM_DETACH_HANDLE, handle);
	fill_attr_in_uint32(cmd, HNS_IB_ATTR_DCA_MEM_DETACH_SQ_INDEX,
			    attr->sq_index);
	ret = execute_ioctl(&ctx->ibv_ctx.context, cmd);
	if (ret)
		verbs_warn(&ctx->ibv_ctx,
			   "failed to detach DCA mem-%u, ret = %d.\n",
			   handle, ret);
}

struct hns_dca_mem_attach_resp {
#define HNS_DCA_ATTACH_OUT_FLAGS_NEW_BUFFER BIT(0)
	uint32_t alloc_flags;
	uint32_t alloc_pages;
};

static int attach_dca_mem(struct hns_roce_context *ctx, uint32_t handle,
			  struct hns_roce_dca_attach_attr *attr,
			  struct hns_dca_mem_attach_resp *resp)
{
	int ret;

	DECLARE_COMMAND_BUFFER(cmd, HNS_IB_OBJECT_DCA_MEM,
			       HNS_IB_METHOD_DCA_MEM_ATTACH, 6);
	fill_attr_in_obj(cmd, HNS_IB_ATTR_DCA_MEM_ATTACH_HANDLE, handle);
	fill_attr_in_uint32(cmd, HNS_IB_ATTR_DCA_MEM_ATTACH_SQ_OFFSET,
			    attr->sq_offset);
	fill_attr_in_uint32(cmd, HNS_IB_ATTR_DCA_MEM_ATTACH_SGE_OFFSET,
			    attr->sge_offset);
	fill_attr_in_uint32(cmd, HNS_IB_ATTR_DCA_MEM_ATTACH_RQ_OFFSET,
			    attr->rq_offset);
	fill_attr_out(cmd, HNS_IB_ATTR_DCA_MEM_ATTACH_OUT_ALLOC_FLAGS,
		      &resp->alloc_flags, sizeof(resp->alloc_flags));
	fill_attr_out(cmd, HNS_IB_ATTR_DCA_MEM_ATTACH_OUT_ALLOC_PAGES,
		      &resp->alloc_pages, sizeof(resp->alloc_pages));
	ret = execute_ioctl(&ctx->ibv_ctx.context, cmd);
	if (ret)
		verbs_err(&ctx->ibv_ctx,
			  "failed to attach DCA mem-%u, ret = %d.\n",
			  handle, ret);

	return ret;
}

static bool add_dca_mem_enabled(struct hns_roce_dca_ctx *ctx,
				uint32_t alloc_size)
{
	bool enable;

	pthread_spin_lock(&ctx->lock);

	if (ctx->unit_size == 0) /* Pool size can't be increased */
		enable = false;
	else if (ctx->max_size == HNS_DCA_MAX_MEM_SIZE) /* Pool size no limit */
		enable = true;
	else /* Pool size doesn't exceed max size */
		enable = (ctx->curr_size + alloc_size) < ctx->max_size;

	pthread_spin_unlock(&ctx->lock);

	return enable;
}

static bool shrink_dca_mem_enabled(struct hns_roce_dca_ctx *ctx)
{
	bool enable;

	pthread_spin_lock(&ctx->lock);
	enable = ctx->mem_cnt > 0 && ctx->min_size < ctx->max_size;
	pthread_spin_unlock(&ctx->lock);

	return enable;
}

static int add_dca_mem(struct hns_roce_context *ctx, uint32_t size)
{
	struct hns_roce_dca_ctx *dca_ctx = &ctx->dca_ctx;
	struct hns_roce_dca_mem *mem;
	int ret;

	if (!add_dca_mem_enabled(&ctx->dca_ctx, size))
		return -ENOMEM;

	/* Step 1: Alloc DCA mem address */
	mem = alloc_dca_mem(
		DIV_ROUND_UP(size, dca_ctx->unit_size) * dca_ctx->unit_size);
	if (!mem)
		return -ENOMEM;

	/* Step 2: Register DCA mem uobject to pin user address */
	ret = register_dca_mem(ctx, dca_mem_to_key(mem), dca_mem_addr(mem, 0),
			       mem->buf.length, &mem->handle);
	if (ret) {
		free_dca_mem(ctx, mem);
		return ret;
	}

	/* Step 3: Add DCA mem node to pool */
	pthread_spin_lock(&dca_ctx->lock);
	list_add_tail(&dca_ctx->mem_list, &mem->entry);
	dca_ctx->mem_cnt++;
	dca_ctx->curr_size += mem->buf.length;
	pthread_spin_unlock(&dca_ctx->lock);

	return 0;
}

void hns_roce_shrink_dca_mem(struct hns_roce_context *ctx)
{
	struct hns_roce_dca_ctx *dca_ctx = &ctx->dca_ctx;
	struct hns_dca_mem_shrink_resp resp = {};
	struct hns_roce_dca_mem *mem;
	int dca_mem_cnt;
	uint32_t handle;
	int ret;

	pthread_spin_lock(&dca_ctx->lock);
	dca_mem_cnt = ctx->dca_ctx.mem_cnt;
	pthread_spin_unlock(&dca_ctx->lock);
	while (dca_mem_cnt > 0 && shrink_dca_mem_enabled(dca_ctx)) {
		resp.free_mems = 0;
		/* Step 1: Use any DCA mem uobject to shrink pool */
		pthread_spin_lock(&dca_ctx->lock);
		mem = list_tail(&dca_ctx->mem_list,
				struct hns_roce_dca_mem, entry);
		handle = mem ? mem->handle : 0;
		pthread_spin_unlock(&dca_ctx->lock);
		if (!mem)
			break;

		ret = shrink_dca_mem(ctx, handle, dca_ctx->min_size, &resp);
		if (ret || likely(resp.free_mems < 1))
			break;

		/* Step 2: Remove shrunk DCA mem node from pool */
		pthread_spin_lock(&dca_ctx->lock);
		mem = key_to_dca_mem(dca_ctx, resp.free_key);
		if (mem) {
			list_del(&mem->entry);
			dca_ctx->mem_cnt--;
			dca_ctx->curr_size -= mem->buf.length;
		}

		handle = mem ? mem->handle : 0;
		pthread_spin_unlock(&dca_ctx->lock);
		if (!mem)
			break;

		/* Step 3: Destroy DCA mem uobject */
		deregister_dca_mem(ctx, handle);
		free_dca_mem(ctx, mem);
		/* No any free memory after deregister 1 DCA mem */
		if (resp.free_mems <= 1)
			break;

		dca_mem_cnt--;
	}
}

static void config_dca_pages(void *addr, struct hns_roce_dca_buf *buf,
			     uint32_t page_index, int page_count)
{
	void **pages = &buf->bufs[page_index];
	int page_size = 1 << buf->shift;
	int i;

	for (i = 0; i < page_count; i++) {
		pages[i] = addr;
		addr += page_size;
	}
}

static int setup_dca_buf(struct hns_roce_context *ctx, uint32_t handle,
			 struct hns_roce_dca_buf *buf, uint32_t page_count)
{
	struct hns_roce_dca_ctx *dca_ctx = &ctx->dca_ctx;
	struct hns_dca_mem_query_resp resp = {};
	struct hns_roce_dca_mem *mem;
	uint32_t idx = 0;
	int ret;

	while (idx < page_count && idx < buf->max_cnt) {
		resp.page_count = 0;
		ret = query_dca_mem(ctx, handle, idx, &resp);
		if (ret)
			return -ENOMEM;
		if (resp.page_count < 1)
			break;

		pthread_spin_lock(&dca_ctx->lock);
		mem = key_to_dca_mem(dca_ctx, resp.key);
		if (mem && resp.offset < mem->buf.length) {
			config_dca_pages(dca_mem_addr(mem, resp.offset),
					 buf, idx, resp.page_count);
		} else {
			pthread_spin_unlock(&dca_ctx->lock);
			break;
		}
		pthread_spin_unlock(&dca_ctx->lock);

		idx += resp.page_count;
	}

	return (idx >= page_count) ? 0 : -ENOMEM;
}

#define DCAN_TO_SYNC_BIT(n) ((n) * HNS_DCA_BITS_PER_STATUS)
#define DCAN_TO_STAT_BIT(n) DCAN_TO_SYNC_BIT(n)

#define MAX_DCA_TRY_LOCK_TIMES 10
bool hns_roce_dca_start_post(struct hns_roce_dca_ctx *ctx, uint32_t dcan)
{
	atomic_bitmap_t *st = ctx->sync_status;
	int try_times = 0;

	if (!st || dcan >= ctx->max_qps)
		return true;

	while (test_and_set_bit_lock(st, DCAN_TO_SYNC_BIT(dcan)))
		if (try_times++ > MAX_DCA_TRY_LOCK_TIMES)
			return false;

	return true;
}

void hns_roce_dca_stop_post(struct hns_roce_dca_ctx *ctx, uint32_t dcan)
{
	atomic_bitmap_t *st = ctx->sync_status;

	if (!st || dcan >= ctx->max_qps)
		return;

	clear_bit_unlock(st, DCAN_TO_SYNC_BIT(dcan));
}

static bool check_dca_is_attached(struct hns_roce_dca_ctx *ctx, uint32_t dcan)
{
	atomic_bitmap_t *st = ctx->buf_status;

	if (!st || dcan >= ctx->max_qps)
		return false;

	return atomic_test_bit(st, DCAN_TO_STAT_BIT(dcan));
}

#define DCA_EXPAND_MEM_TRY_TIMES	3
int hns_roce_attach_dca_mem(struct hns_roce_context *ctx, uint32_t handle,
			    struct hns_roce_dca_attach_attr *attr,
			    uint32_t size, struct hns_roce_dca_buf *buf)
{
	uint32_t buf_pages = size >> buf->shift;
	struct hns_dca_mem_attach_resp resp = {};
	bool is_new_buf = true;
	int try_times = 0;
	int ret = 0;

	if (!attr->force && check_dca_is_attached(&ctx->dca_ctx, buf->dcan))
		return 0;

	do {
		resp.alloc_pages = 0;
		ret = attach_dca_mem(ctx, handle, attr, &resp);
		if (ret)
			break;

		if (resp.alloc_pages >= buf_pages) {
			is_new_buf = !!(resp.alloc_flags &
				     HNS_DCA_ATTACH_OUT_FLAGS_NEW_BUFFER);
			break;
		}

		ret = add_dca_mem(ctx, size);
		if (ret)
			break;
	} while (try_times++ < DCA_EXPAND_MEM_TRY_TIMES);

	if (ret || resp.alloc_pages < buf_pages) {
		verbs_err(&ctx->ibv_ctx,
			  "failed to attach, size %u count %u != %u, ret = %d.\n",
			  size, buf_pages, resp.alloc_pages, ret);
		return -ENOMEM;
	}

	/* No need config user address if DCA config not changed */
	if (!is_new_buf && buf->bufs[0])
		return 0;

	return setup_dca_buf(ctx, handle, buf, buf_pages);
}
