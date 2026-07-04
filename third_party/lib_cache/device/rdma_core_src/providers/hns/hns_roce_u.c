/*
 * Copyright (c) 2016-2017 Hisilicon Limited.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#include "hns_roce_u.h"

static void hns_roce_free_context(struct ibv_context *ibctx);
static void hns_roce_u_async_event(struct ibv_context *context, struct ibv_async_event *event);

#ifndef PCI_VENDOR_ID_HUAWEI
#define PCI_VENDOR_ID_HUAWEI			0x19E5
#endif

static const struct verbs_match_ent hca_table[] = {
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA222, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA223, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA224, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA225, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA226, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA227, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA228, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA229, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA22A, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA22C, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA22D, &hns_roce_u_hw_v2),
	VERBS_PCI_MATCH(PCI_VENDOR_ID_HUAWEI, 0xA22F, &hns_roce_u_hw_v2),
	{}
};

static const struct verbs_context_ops hns_common_ops = {
	.alloc_mw = hns_roce_u_alloc_mw,
	.alloc_pd = hns_roce_u_alloc_pd,
	.async_event = hns_roce_u_async_event,
	.bind_mw = hns_roce_u_bind_mw,
	.cq_event = hns_roce_u_cq_event,
	.create_cq = hns_roce_u_create_cq,
	.create_cq_ex = hns_roce_u_create_cq_ex,
	.create_qp = hns_roce_u_create_qp,
	.create_qp_ex = hns_roce_u_create_qp_ex,
	.dealloc_mw = hns_roce_u_dealloc_mw,
	.dealloc_pd = hns_roce_u_dealloc_pd,
	.dereg_mr = hns_roce_u_dereg_mr,
	.destroy_cq = hns_roce_u_destroy_cq,
	.modify_cq = hns_roce_u_modify_cq,
	.query_device_ex = hns_roce_u_query_device,
	.query_port = hns_roce_u_query_port,
	.query_qp = hns_roce_u_query_qp,
	.reg_mr = hns_roce_u_reg_mr,
	.rereg_mr = hns_roce_u_rereg_mr,
	.create_srq = hns_roce_u_create_srq,
	.create_srq_ex = hns_roce_u_create_srq_ex,
	.modify_srq = hns_roce_u_modify_srq,
	.query_srq = hns_roce_u_query_srq,
	.destroy_srq = hns_roce_u_destroy_srq,
	.free_context = hns_roce_free_context,
	.create_ah = hns_roce_u_create_ah,
	.destroy_ah = hns_roce_u_destroy_ah,
	.open_xrcd = hns_roce_u_open_xrcd,
	.close_xrcd = hns_roce_u_close_xrcd,
	.open_qp = hns_roce_u_open_qp,
	.get_srq_num = hns_roce_u_get_srq_num,
	.alloc_td = hns_roce_u_alloc_td,
	.dealloc_td = hns_roce_u_dealloc_td,
	.alloc_parent_domain = hns_roce_u_alloc_pad,
};

static int mmap_dca(struct hns_roce_context *ctx, int cmd_fd,
		    int page_size, size_t size, uint64_t mmap_key)
{
	struct hns_roce_dca_ctx *dca_ctx = &ctx->dca_ctx;
	void *addr;

	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, cmd_fd,
		mmap_key);
	if (addr == MAP_FAILED) {
		verbs_err(&ctx->ibv_ctx, "failed to mmap() dca prime qp.\n");
		return -EINVAL;
	}

	dca_ctx->buf_status = addr;
	dca_ctx->sync_status = addr + size / 2;

	return 0;
}

bool hnsdv_is_supported(struct ibv_device *device)
{
	return is_hns_dev(device);
}

struct ibv_context *hnsdv_open_device(struct ibv_device *device,
				      struct hnsdv_context_attr *attr)
{
	if (!is_hns_dev(device)) {
		errno = EOPNOTSUPP;
		return NULL;
	}

	return verbs_open_device(device, attr);
}

static void set_dca_pool_param(struct hns_roce_context *ctx,
			       struct hnsdv_context_attr *attr, int page_size)
{
	struct hns_roce_dca_ctx *dca_ctx = &ctx->dca_ctx;

	if (attr->comp_mask & HNSDV_CONTEXT_MASK_DCA_UNIT_SIZE)
		dca_ctx->unit_size = align(attr->dca_unit_size, page_size);
	else
		dca_ctx->unit_size = page_size * HNS_DCA_DEFAULT_UNIT_PAGES;

	/* The memory pool cannot be expanded, only init the DCA context. */
	if (dca_ctx->unit_size == 0)
		return;

	/* If not set, the memory pool can be expanded unlimitedly. */
	if (attr->comp_mask & HNSDV_CONTEXT_MASK_DCA_MAX_SIZE)
		dca_ctx->max_size = DIV_ROUND_UP(attr->dca_max_size,
						 dca_ctx->unit_size) *
					dca_ctx->unit_size;
	else
		dca_ctx->max_size = HNS_DCA_MAX_MEM_SIZE;

	/* If not set, the memory pool cannot be shrunk. */
	if (attr->comp_mask & HNSDV_CONTEXT_MASK_DCA_MIN_SIZE)
		dca_ctx->min_size = DIV_ROUND_UP(attr->dca_min_size,
						 dca_ctx->unit_size) *
					dca_ctx->unit_size;
	else
		dca_ctx->min_size = HNS_DCA_MAX_MEM_SIZE;

	verbs_debug(&ctx->ibv_ctx,
		    "Support DCA, unit %d, max %ld, min %ld Bytes.\n",
		    dca_ctx->unit_size, dca_ctx->max_size, dca_ctx->min_size);
}

static int init_dca_context(struct hns_roce_context *ctx, int cmd_fd,
			    struct hns_roce_alloc_ucontext_resp *resp,
			    struct hnsdv_context_attr *attr,
			    int page_size)
{
	struct hns_roce_dca_ctx *dca_ctx = &ctx->dca_ctx;
	uint64_t mmap_key = resp->dca_mmap_key;
	int mmap_size = resp->dca_mmap_size;
	int max_qps = resp->dca_qps;
	int ret;

	if (!(ctx->config & HNS_ROCE_UCTX_RSP_DCA_FLAGS))
		return 0;

	dca_ctx->unit_size = 0;
	dca_ctx->mem_cnt = 0;

	list_head_init(&dca_ctx->mem_list);
	ret = pthread_spin_init(&dca_ctx->lock, PTHREAD_PROCESS_PRIVATE);
	if (ret)
		return ret;

	if (!attr || !(attr->flags & HNSDV_CONTEXT_FLAGS_DCA))
		return 0;

	set_dca_pool_param(ctx, attr, page_size);

	if (mmap_key) {
		const unsigned int bits_per_qp = 2 * HNS_DCA_BITS_PER_STATUS;

		if (!mmap_dca(ctx, cmd_fd, page_size, mmap_size, mmap_key)) {
			dca_ctx->status_size = mmap_size;
			dca_ctx->max_qps = min_t(int, max_qps,
						 mmap_size *  8 / bits_per_qp);
		}
	}

	return 0;
}

static void uninit_dca_context(struct hns_roce_context *ctx)
{
	struct hns_roce_dca_ctx *dca_ctx = &ctx->dca_ctx;

	if (!(ctx->config & HNS_ROCE_UCTX_RSP_DCA_FLAGS))
		return;

	pthread_spin_lock(&dca_ctx->lock);
	hns_roce_cleanup_dca_mem(ctx);
	pthread_spin_unlock(&dca_ctx->lock);
	if (dca_ctx->buf_status)
		munmap(dca_ctx->buf_status, dca_ctx->status_size);

	pthread_spin_destroy(&dca_ctx->lock);
}

static int init_reset_context(struct hns_roce_context *ctx, int cmd_fd,
			struct hns_roce_alloc_ucontext_resp *resp,
			int page_size)
{
	uint64_t reset_mmap_key = resp->reset_mmap_key;

	/* The reset mmap key is 0, which means it is not supported. */
	if (reset_mmap_key == 0)
		return 0;

	ctx->reset_state = mmap(NULL, page_size, PROT_READ, MAP_SHARED,
				cmd_fd, reset_mmap_key);
	if (ctx->reset_state == MAP_FAILED)
		return -ENOMEM;

	return 0;
}

static int hns_roce_mmap(struct hns_roce_device *hr_dev,
			 struct hns_roce_context *context, int cmd_fd)
{
	int page_size = hr_dev->page_size;

	context->uar = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			    MAP_SHARED, cmd_fd, 0);
	if (context->uar == MAP_FAILED)
		return -ENOMEM;

	return 0;
}

static uint32_t calc_table_shift(uint32_t entry_count, uint32_t size_shift)
{
	uint32_t count_shift = hr_ilog32(entry_count);

	return count_shift > size_shift ? count_shift - size_shift : 0;
}

static void ucontext_set_cmd(struct hns_roce_alloc_ucontext *cmd,
			     struct hnsdv_context_attr *attr)
{
	cmd->config |= HNS_ROCE_EXSGE_FLAGS | HNS_ROCE_RQ_INLINE_FLAGS |
		       HNS_ROCE_CQE_INLINE_FLAGS;

	if (!attr || !(attr->flags & HNSDV_CONTEXT_FLAGS_DCA))
		return;

	cmd->config |= HNS_ROCE_UCTX_CONFIG_DCA;

	if (attr->comp_mask & HNSDV_CONTEXT_MASK_DCA_PRIME_QPS) {
		cmd->comp |= HNS_ROCE_ALLOC_UCTX_COMP_DCA_MAX_QPS;
		cmd->dca_max_qps = attr->dca_prime_qps;
	}
}

static void hns_roce_u_async_event(struct ibv_context *context, struct ibv_async_event *event)
{
	struct hns_roce_device *hr_dev = to_hr_dev(context->device);

	if (event->event_type == IBV_EVENT_DEVICE_FATAL) {
		hr_dev->state = HNS_ROCE_DEVICE_STATE_FATAL;
	}
}

static struct verbs_context *hns_roce_alloc_context(struct ibv_device *ibdev,
						    int cmd_fd,
						    void *private_data)
{
	struct hnsdv_context_attr *ctx_attr = private_data;
	struct hns_roce_device *hr_dev = to_hr_dev(ibdev);
	struct hns_roce_alloc_ucontext_resp resp = {};
	struct hns_roce_alloc_ucontext cmd = {};
	struct ibv_device_attr dev_attrs;
	struct hns_roce_context *context;
	int i;

	context = verbs_init_and_alloc_context(ibdev, cmd_fd, context, ibv_ctx,
					       RDMA_DRIVER_HNS);
	if (!context)
		return NULL;

	ucontext_set_cmd(&cmd, ctx_attr);
	if (ibv_cmd_get_context(&context->ibv_ctx, &cmd.ibv_cmd, sizeof(cmd),
				&resp.ibv_resp, sizeof(resp)))
		goto err_free;

	hr_dev->mac_type = resp.mac_type;
	context->compat_kernel = 0;

	if (!resp.cqe_size) {
		context->compat_kernel = 1;
	}
	context->cqe_size = HNS_ROCE_CQE_SIZE;

	context->config = resp.config;
	if (resp.config & HNS_ROCE_RSP_EXSGE_FLAGS)
		context->max_inline_data = resp.max_inline_data;

	context->qp_table_shift = calc_table_shift(resp.qp_tab_size,
						   HNS_ROCE_QP_TABLE_BITS);
	context->qp_table_mask = (1 << context->qp_table_shift) - 1;
	pthread_mutex_init(&context->qp_table_mutex, NULL);
	for (i = 0; i < HNS_ROCE_QP_TABLE_SIZE; ++i)
		context->qp_table[i].refcnt = 0;

	context->srq_table_shift = calc_table_shift(resp.srq_tab_size,
						    HNS_ROCE_SRQ_TABLE_BITS);
	context->srq_table_mask = (1 << context->srq_table_shift) - 1;
	pthread_mutex_init(&context->srq_table_mutex, NULL);
	for (i = 0; i < HNS_ROCE_SRQ_TABLE_SIZE; ++i)
		context->srq_table[i].refcnt = 0;

	if (hns_roce_u_query_device(&context->ibv_ctx.context, NULL,
				    container_of(&dev_attrs,
						 struct ibv_device_attr_ex,
						 orig_attr),
				    sizeof(dev_attrs)))
		goto err_free;

	hr_dev->hw_version = dev_attrs.hw_ver;
	context->max_qp_wr = dev_attrs.max_qp_wr;
	context->max_sge = dev_attrs.max_sge;
	context->max_cqe = dev_attrs.max_cqe;
	context->max_srq_wr = dev_attrs.max_srq_wr;
	context->max_srq_sge = dev_attrs.max_srq_sge;

	if (init_dca_context(context, cmd_fd,
			     &resp, ctx_attr, hr_dev->page_size))
		goto err_free;

	if (init_reset_context(context, cmd_fd, &resp, hr_dev->page_size))
		goto reset_free;

	if (hns_roce_mmap(hr_dev, context, cmd_fd))
		goto uar_free;

	if (hns_roce_init_ai_ctx(&context->ibv_ctx.context))
		goto err_ai_ctx;

	if (hns_roce_lite_get_devid(context->ai_ctx.func_cap.chip_id,
								context->ai_ctx.func_cap.die_id, &context->dev_id))
		goto err_get_devid;

	pthread_spin_init(&context->uar_lock, PTHREAD_PROCESS_PRIVATE);

	verbs_set_ops(&context->ibv_ctx, &hns_common_ops);
	verbs_set_ops(&context->ibv_ctx, &hr_dev->u_hw->hw_ops);

	return &context->ibv_ctx;

err_get_devid:
	hns_roce_uninit_ai_ctx(&context->ibv_ctx.context);
err_ai_ctx:
	munmap(context->uar, hr_dev->page_size);
uar_free:
	if (context->reset_state)
		munmap(context->reset_state, hr_dev->page_size);
reset_free:
	uninit_dca_context(context);
err_free:
	verbs_uninit_context(&context->ibv_ctx);
	free(context);
	return NULL;
}

static void hns_roce_free_context(struct ibv_context *ibctx)
{
	struct hns_roce_device *hr_dev = to_hr_dev(ibctx->device);
	struct hns_roce_context *context = to_hr_ctx(ibctx);

	hns_roce_uninit_ai_ctx(&context->ibv_ctx.context);
	munmap(context->uar, hr_dev->page_size);
	if (context->reset_state)
		munmap(context->reset_state, hr_dev->page_size);
	uninit_dca_context(context);
	verbs_uninit_context(&context->ibv_ctx);
	free(context);
}

static void hns_uninit_device(struct verbs_device *verbs_device)
{
	struct hns_roce_device *dev = to_hr_dev(&verbs_device->device);

	hns_roce_close_slog_so();
	free(dev);
}

static struct verbs_device *hns_device_alloc(struct verbs_sysfs_dev *sysfs_dev)
{
	struct hns_roce_device *dev;
	int ret;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->u_hw = sysfs_dev->match->driver_data;
	dev->hw_version = dev->u_hw->hw_version;
	dev->page_size = sysconf(_SC_PAGESIZE);

	/* slog ops api init */
	ret = hns_roce_open_slog_so();
	if (ret) {
		printf("open libslog.so failed, ret %d.", ret);
		return &dev->ibv_dev;
	}

	ret = hns_roce_slog_api_init();
	if (ret) {
		printf("init libslog.so api failed, ret %d.", ret);
		hns_roce_close_slog_so();
	}

	return &dev->ibv_dev;
}

static const struct verbs_device_ops hns_roce_dev_ops = {
	.name = "hns",
	.match_min_abi_version = 0,
	.match_max_abi_version = INT_MAX,
	.match_table = hca_table,
	.alloc_device = hns_device_alloc,
	.uninit_device = hns_uninit_device,
	.alloc_context = hns_roce_alloc_context,
};

bool is_hns_dev(struct ibv_device *device)
{
	struct verbs_device *verbs_device = verbs_get_device(device);

	return verbs_device->ops == &hns_roce_dev_ops;
}
PROVIDER_DRIVER(hns, hns_roce_dev_ops);
