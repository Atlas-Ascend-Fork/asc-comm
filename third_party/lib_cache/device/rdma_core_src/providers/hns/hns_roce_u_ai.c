#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ccan/minmax.h>
#include <sys/ioctl.h>

#include "hns_roce_u_abi.h"
#include "hns_roce_u.h"
#include "hns_roce_u_db.h"
#include "hns_roce_u_hw_v2.h"
#include "hns_roce_u_ai.h"

#define HNS_ROCE_AI_ENABLE  1
#define PAGE_SHIFT          12

#define HNS_ROCE_DEFAULT_TEMP_DEPTH     6
#define HNS_ROCE_DEFAULT_SQ_DEPTH       128
#define HNS_ROCE_DEFAULT_QP_NUM         8192
static unsigned int g_temp_depth = HNS_ROCE_DEFAULT_TEMP_DEPTH;
static unsigned int g_sq_depth = HNS_ROCE_DEFAULT_SQ_DEPTH;
static unsigned int g_qp_num = HNS_ROCE_DEFAULT_QP_NUM;

static void *g_roce_hal_api_handle = NULL;
static pthread_mutex_t g_roce_hal_api_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_roce_hal_refcount = 0;
static struct roce_hal_api_ops g_roce_hal_api_ops = { 0 };

static int hns_roce_init_hal_api(void)
{
	pthread_mutex_lock(&g_roce_hal_api_lock);
	if (g_roce_hal_api_handle == NULL) {
		g_roce_hal_api_handle = dlopen("libhns-rdma-hal.so", RTLD_NOW);
		if (g_roce_hal_api_handle == NULL) {
			pthread_mutex_unlock(&g_roce_hal_api_lock);
			return -EINVAL;
		}

		g_roce_hal_api_ops.hns_roce_hal_alloc_buf =
			(int (*)(void **buf, unsigned int *length, unsigned int size, unsigned int page_size, unsigned int dev_id))
				dlsym(g_roce_hal_api_handle, "hns_roce_hal_alloc_buf");
		if (g_roce_hal_api_ops.hns_roce_hal_alloc_buf == NULL) {
			(void)dlclose(g_roce_hal_api_handle);
			pthread_mutex_unlock(&g_roce_hal_api_lock);
			printf("g_roce_hal_api_ops.hns_roce_hal_alloc_buf is NULL!\n");
			return -EINVAL;
		}

		g_roce_hal_api_ops.hns_roce_hal_get_dev_id =
			(int (*)(unsigned int chip_id, unsigned int die_id, unsigned int *dev_id))
				dlsym(g_roce_hal_api_handle, "hns_roce_hal_get_dev_id");
		if (g_roce_hal_api_ops.hns_roce_hal_get_dev_id == NULL) {
			(void)dlclose(g_roce_hal_api_handle);
			pthread_mutex_unlock(&g_roce_hal_api_lock);
			printf("g_roce_hal_api_ops.hns_roce_hal_get_dev_id is NULL!\n");
			return -EINVAL;
		}

		g_roce_hal_api_ops.hns_roce_hal_alloc_ai_buf =
			(int (*)(void **buf, unsigned int *length, unsigned int size, unsigned int page_size,
				unsigned int dev_id, unsigned int ai_op_support, unsigned int grp_id))
				dlsym(g_roce_hal_api_handle, "hns_roce_hal_alloc_ai_buf");
		if (!g_roce_hal_api_ops.hns_roce_hal_alloc_ai_buf) {
			(void)dlclose(g_roce_hal_api_handle);
			pthread_mutex_unlock(&g_roce_hal_api_lock);
			printf("g_roce_hal_api_ops.hns_roce_hal_alloc_ai_buf is NULL!\n");
			return -EINVAL;
		}

		g_roce_hal_api_ops.hns_roce_hal_free_ai_buf = (int (*)(void *buf))
			dlsym(g_roce_hal_api_handle, "hns_roce_hal_free_ai_buf");
		if (g_roce_hal_api_ops.hns_roce_hal_free_ai_buf == NULL) {
			(void)dlclose(g_roce_hal_api_handle);
			pthread_mutex_unlock(&g_roce_hal_api_lock);
			printf("g_roce_hal_api_ops.hns_roce_hal_free_ai_buf is NULL!\n");
			return -EINVAL;
		}

		atomic_fetch_add(&g_roce_hal_refcount, 1);
		pthread_mutex_unlock(&g_roce_hal_api_lock);
		return 0;
	}

	atomic_fetch_add(&g_roce_hal_refcount, 1);
	pthread_mutex_unlock(&g_roce_hal_api_lock);
	return 0;
}

static void hns_roce_deinit_hal_api(void)
{
	pthread_mutex_lock(&g_roce_hal_api_lock);
	if (atomic_fetch_sub(&g_roce_hal_refcount, 1) == 1) {
		if (g_roce_hal_api_handle != NULL) {
			(void)dlclose(g_roce_hal_api_handle);
			g_roce_hal_api_handle = NULL;
		}
	}
	pthread_mutex_unlock(&g_roce_hal_api_lock);
}

static void hns_roce_u_v2_sq_init_nop_wqe(struct hns_roce_qp *qp);

static struct ibv_mr *hns_roce_u_reg_ai_mr(struct ibv_pd *pd, void *addr, size_t length, int access,
    struct ibv_mr_ai_attr *attr)
{
    int ret;
    struct verbs_mr *vmr = NULL;
    struct hns_roce_reg_ai_mr cmd;
    struct ib_uverbs_reg_mr_resp resp;
    struct hns_roce_context *ctx = NULL;

    if (pd == NULL || pd->context == NULL) {
        roce_err("pd NULL ptr or pd->context NULL ptr.");
        return NULL;
    }

    ctx = to_hr_ctx(pd->context);
    if (ctx->ai_ctx.enable != HNS_ROCE_AI_ENABLE) {
        roce_event("not support reg ai mr.");
        return NULL;
    }

    if (addr == NULL) {
        roce_err("addr NULL ptr.");
        return NULL;
    }

    if (attr == NULL) {
        roce_err("attr NULL ptr.");
        return NULL;
    }

    if (length == 0)  {
        roce_err("length(%ld) or tgid(%d) err.", length, attr->tgid);
        return NULL;
    }

    vmr = calloc(1, sizeof(struct verbs_mr));
    if (vmr == NULL) {
        roce_err("calloc mr failed!");
        return NULL;
    }

    memcpy(&cmd.ai_attr, attr, sizeof(struct ibv_mr_ai_attr));
    ret = ibv_cmd_reg_mr(pd, addr, length, (uintptr_t)addr, access, vmr,
                         &(cmd.ibv_cmd), sizeof(cmd), &resp, sizeof(resp));
    if (ret) {
        roce_err("reg ai mr failed, ret[%d]", ret);
        free(vmr);
        return NULL;
    }

    return &vmr->ibv_mr;
}

static void hns_roce_set_qp_init_attrx(struct ibv_pd *pd, struct ibv_qp_init_attr *attr,
    struct ibv_qp_init_attr_ex *attrx, struct ibv_qp_ai_attr *ai_attr)
{
    memcpy(attrx, attr, sizeof(*attr));
    attrx->comp_mask = IBV_QP_INIT_ATTR_PD;
    attrx->pd = pd;
}

static int hns_roce_init_sq_rq_lock(struct hns_roce_qp *qp)
{
    hns_roce_init_qp_indices(qp);

    if (pthread_spin_init(&qp->sq.hr_lock.lock, PTHREAD_PROCESS_PRIVATE)) {
        roce_err("pthread_spin_init sq failed!");
        return -1;
    }

    if (pthread_spin_init(&qp->rq.hr_lock.lock, PTHREAD_PROCESS_PRIVATE)) {
        roce_err("pthread_spin_init rq failed!");
        pthread_spin_destroy(&qp->sq.hr_lock.lock);
        return -1;
    }

    return 0;
}

static void hns_roce_deinit_sq_rq_lock(struct hns_roce_qp *qp)
{
    pthread_spin_destroy(&qp->rq.hr_lock.lock);
    pthread_spin_destroy(&qp->sq.hr_lock.lock);
}

static void hns_roce_store_qp_ai_attr(struct hns_roce_context *context, struct hns_roce_qp *qp,
    struct ibv_qp_ai_attr *ai_attr, struct ibv_qp_ai_resp *ai_resp)
{
#define DB_INDEX_SQ_DEPTH_SHIFT 12 /* 12:15 */
#define DB_INDEX_SQ_DEPTH_MASK 0xf
#define DB_INDEX_DIE_ID_SHIFT 16 /* 16:23 */
#define DB_INDEX_DIE_ID_MASK 0xff
    int rts_vf_id;

    qp->qp_mode = ai_attr->qp_mode;
    qp->qp_cstm_flag = ai_attr->qp_cstm_flag;
    ai_resp->db_index = 0;

    if (context->ai_ctx.func_cap.total_vf_num == 0) {
        rts_vf_id = context->ai_ctx.func_cap.pf_id;
    } else {
        rts_vf_id = context->ai_ctx.func_cap.pf_num + context->ai_ctx.func_cap.vf_id;
    }

    if (ai_attr->qp_mode == HNS_ROCE_QP_AI_MODE_GDR_ASYN || ai_attr->qp_mode == HNS_ROCE_QP_AI_MODE_OP) {
        ai_resp->db_index |= ((hr_ilog32(qp->sq.wqe_cnt) & DB_INDEX_SQ_DEPTH_MASK) << DB_INDEX_SQ_DEPTH_SHIFT) |
                             ((context->ai_ctx.func_cap.die_id & DB_INDEX_DIE_ID_MASK) << DB_INDEX_DIE_ID_SHIFT) |
                             rts_vf_id;
    }

    ai_resp->sq_index = qp->shm.shm_attr.index;
}

static void hns_roce_record_qp_attr(struct hns_roce_qp *qp, struct hns_roce_device *hr_dev,
    struct hns_roce_context *context, struct ibv_qp_init_attr_ex *attrx, struct hns_roce_create_ai_qp_resp *resp)
{
    if (qp->rq.wqe_cnt) {
        qp->rq.wqe_cnt = attrx->cap.max_recv_wr;
        qp->rq.max_gs = attrx->cap.max_recv_sge;

        /* adjust rq maxima to not exceed reported device maxima */
        attrx->cap.max_recv_wr =
            min(context->max_qp_wr, attrx->cap.max_recv_wr);
        attrx->cap.max_recv_sge -= qp->rq.rsv_sge;
        qp->rq.max_post = attrx->cap.max_recv_wr;
    }

    qp->max_inline_data = attrx->cap.max_inline_data;
    qp->flags = resp->cap_flags;

    if (qp->flags & HNS_ROCE_QP_CAP_DIRECT_WQE) {
        qp->sq.db_reg = qp->dwqe_page;
    } else if (qp->qp_mode == HNS_ROCE_QP_AI_MODE_NOR && qp->buf.ai_op_support != 0 && context->tgid != 0) {
        // normal qp use tgid_uar to record db when ai_op_support set
        qp->sq.db_reg = context->tgid_uar + ROCEE_VF_DB_CFG0_OFFSET;
    } else {
        qp->sq.db_reg = context->uar + ROCEE_VF_DB_CFG0_OFFSET;
    }
}

static int hns_roce_create_qp_cmd(struct hns_roce_context *context, struct hns_roce_qp *qp,
    struct ibv_qp_init_attr_ex *attrx, struct hns_roce_create_ai_qp *cmd, struct hns_roce_create_ai_qp_resp *resp)
{
    int ret;

    cmd->buf_addr = (uintptr_t)(qp->buf.buf + qp->buf.offset);
    cmd->log_sq_stride = qp->sq.wqe_shift;
    cmd->log_sq_bb_count = hr_ilog32(qp->sq.wqe_cnt);
    cmd->sdb_addr = (uintptr_t)qp->sdb;
    cmd->db_addr = (uintptr_t)qp->rdb;

    ret = ibv_cmd_create_qp_ex(&context->ibv_ctx.context, &qp->verbs_qp,
        attrx, &cmd->ibv_cmd, sizeof(*cmd), &resp->base, sizeof(*resp));
    if (ret) {
        roce_err("hns_roce_cmd_create_qp_ex error %d!", ret);
        return ret;
    }

    ret = hns_roce_store_qp(context, qp);
    if (ret) {
        roce_err("hns_roce_store_qp failed!");
        ibv_cmd_destroy_qp(&qp->verbs_qp.qp);
        return ret;
    }

    return 0;
}

enum {
	HNS_ROCE_QP_CREATE_CIRE = 1 << 2,
	HNS_ROCE_QP_CREATE_MANAGED_SEND = 1 << 3,
	HNS_ROCE_QP_CREATE_MANAGED_RECV = 1 << 4,
	HNS_ROCE_QP_CREATE_DCA_MODE = HNS_ROCE_QP_CREATE_MANAGED_SEND,
};

enum { HNS_ROCE_CREATE_QP_EX2_SUP_CREATE_FLAGS =
	       HNS_ROCE_QP_CREATE_CIRE | HNS_ROCE_QP_CREATE_MANAGED_SEND |
	       HNS_ROCE_QP_CREATE_MANAGED_RECV | HNS_ROCE_QP_CREATE_DCA_MODE,
};

enum { HNS_ROCE_CREATE_QP_EX2_COMP_MASK = IBV_QP_INIT_ATTR_CREATE_FLAGS,
};

enum { HNS_ROCE_CREATE_QP_COMP_MASK = IBV_QP_INIT_ATTR_PD |
				      IBV_QP_INIT_ATTR_XRCD |
				      IBV_QP_INIT_ATTR_CREATE_FLAGS,
};

static int verify_qp(struct ibv_qp_init_attr_ex *attr)
{
	if (attr->comp_mask & ~HNS_ROCE_CREATE_QP_COMP_MASK) {
		roce_err("Unsuppot comp %x.\n", attr->comp_mask);
		return EINVAL;
	}

	if (attr->comp_mask & HNS_ROCE_CREATE_QP_EX2_COMP_MASK &&
	    attr->create_flags & ~HNS_ROCE_CREATE_QP_EX2_SUP_CREATE_FLAGS) {
		roce_err("Unsuppot creation flags %x.\n", attr->create_flags);
		return EINVAL;
	}

	if ((attr->qp_type == IBV_QPT_RC ||
	     attr->qp_type == IBV_QPT_UD ||
	     attr->qp_type == IBV_QPT_XRC_SEND) &&
	    !(attr->comp_mask & IBV_QP_INIT_ATTR_PD)) {
		roce_err("unsupported comp, qp_type = 0x%x, comp_mask = %d.\n",
		       attr->qp_type, attr->comp_mask);
		return EINVAL;
	}

	if (attr->qp_type == IBV_QPT_XRC_RECV &&
	    !(attr->comp_mask & IBV_QP_INIT_ATTR_XRCD)) {
		roce_err("unsupported comp, qp_type = 0x%x, comp_mask = %d.\n",
		       attr->qp_type, attr->comp_mask);
		return EINVAL;
	}

	if (attr->qp_type != IBV_QPT_RC &&
	    attr->qp_type != IBV_QPT_UD &&
	    attr->qp_type != IBV_QPT_XRC_SEND &&
	    attr->qp_type != IBV_QPT_XRC_RECV) {
		roce_err("unsupported qp type, qp_type = 0x%x.\n", attr->qp_type);
		return EINVAL;
	}

	return 0;
}

static int hns_roce_verify_qp(struct ibv_qp_init_attr_ex *attr,
		       struct hns_roce_context *context)
{
	uint32_t min_wqe_num = HNS_ROCE_V2_MIN_WQE_NUM;

	if (verify_qp(attr))
		return EINVAL;

	if (attr->qp_type != IBV_QPT_XRC_RECV &&
	    !attr->cap.max_send_wr)
		return EINVAL;

	if (attr->srq) {
		attr->cap.max_recv_wr = 0;
		attr->cap.max_recv_sge = 0;
	}

	if (attr->cap.max_send_wr > context->max_qp_wr ||
	    attr->cap.max_send_sge > context->max_sge ||
	    attr->cap.max_recv_sge > context->max_sge)
		return EINVAL;

	if (attr->cap.max_send_wr < min_wqe_num) {
		roce_err("change sq depth from %u to minimum %u.\n",
			   attr->cap.max_send_wr, min_wqe_num);
		attr->cap.max_send_wr = min_wqe_num;
	}

	if (attr->cap.max_recv_wr) {
		if (attr->cap.max_recv_wr < min_wqe_num) {
			roce_err("change rq depth from %u to minimum %u.\n",
				   attr->cap.max_recv_wr, min_wqe_num);
			attr->cap.max_recv_wr = min_wqe_num;
		}
		if (!attr->cap.max_recv_sge ||
		    attr->cap.max_recv_wr > context->max_qp_wr)
			return EINVAL;
	}

	return 0;
}

int hns_roce_lite_alloc_buf(void **buf, unsigned int *length, unsigned int size, unsigned int page_size,
	unsigned int dev_id)
{
	return g_roce_hal_api_ops.hns_roce_hal_alloc_buf(buf, length, size, page_size, dev_id);
}

int hns_roce_lite_get_devid(unsigned int chip_id, unsigned int die_id, unsigned int *dev_id)
{
	return g_roce_hal_api_ops.hns_roce_hal_get_dev_id(chip_id, die_id, dev_id);
}

int hns_roce_ai_alloc_buf(void **buf, unsigned int *length, unsigned int size, unsigned int page_size,
	unsigned int dev_id, unsigned int ai_op_support, unsigned int grp_id)
{
	if (!ai_op_support) {
		*buf = calloc(1, size);
		if (*buf == NULL) {
			roce_err("calloc failed, size = %u", size);
			return -ENOMEM;
		}
		return 0;
	}

	return g_roce_hal_api_ops.hns_roce_hal_alloc_ai_buf(buf, length, size, page_size, dev_id, grp_id);
}

void hns_roce_ai_free_buf(unsigned int ai_op_support, void *buf)
{
	int ret;

	if (!ai_op_support) {
		free(buf);
		return;
	}

	ret = g_roce_hal_api_ops.hns_roce_hal_free_ai_buf(buf);
	if (ret) {
		roce_err("hns_roce_hal_free_ai_buf failed, ret = %d", ret);
	}
	return;
}

static struct ibv_qp *hns_roce_u_create_ai_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr,
	struct ibv_qp_ai_attr *ai_attr, struct ibv_qp_ai_resp *ai_resp)
{
	struct ibv_qp_init_attr_ex attrx = {};
	struct hns_roce_device *hr_dev = to_hr_dev(pd->context->device);
	struct hns_roce_context *context = to_hr_ctx(pd->context);
	struct hns_roce_create_ai_qp cmd = {};
	struct hns_roce_create_ai_qp_resp resp = {};
	struct hns_roce_qp *qp = NULL;
	unsigned int length;
	int ret;

	if (context->ai_ctx.enable != HNS_ROCE_AI_ENABLE) {
		roce_event("not support create ai qp.");
		return NULL;
	}

	hns_roce_set_qp_init_attrx(pd, attr, &attrx, ai_attr);
	if (hns_roce_verify_qp(&attrx, context)) {
		roce_err("verify qp failed!");
		return NULL;
	}

	ret = hns_roce_ai_alloc_buf(&qp, &length, sizeof(*qp), HNS_HW_PAGE_SIZE, context->dev_id,
		ai_attr->ai_op_support, ai_attr->grp_id);
	if (ret || !qp) {
		roce_err("hns_roce_ai_alloc_buf qp failed! ret = %d", ret);
		return NULL;
	}

	qp->dev_id = context->dev_id;
	if (ai_attr->qp_mode == HNS_ROCE_QP_AI_MODE_OP && ai_attr->lite_op_support) {
		qp->buf_type = HNS_ROCE_BUF_TYPE_LITE_ALIGN_4KB;
		if (ai_attr->mem_align == LITE_ALIGN_2MB) {
			qp->buf_type = HNS_ROCE_BUF_TYPE_LITE_ALIGN_2MB;
			qp->buf.mem_idx = ai_attr->mem_idx;
			qp->buf.mem_align = ai_attr->mem_align;
		}
	} else if (ai_attr->ai_op_support) {
		qp->buf_type = HNS_ROCE_BUF_TYPE_AI_ALIGN_4KB;
		qp->buf.grp_id = ai_attr->grp_id;
		qp->buf.ai_op_support = ai_attr->ai_op_support;
	}
	hns_roce_set_qp_params(&attrx, qp, context);

	if (hns_roce_alloc_qp_buf(&attrx, NULL, qp, context)) {
		roce_err("hns_roce_alloc_qp_buf failed!");
		goto err_free_obj;
	}

	if (hns_roce_init_sq_rq_lock(qp)) {
		roce_err("pthread_spin_init failed!");
		goto err_free_buf;
	}

	cmd.qp_mode = (__u32)ai_attr->qp_mode;
	cmd.udp_sport = ai_attr->udp_sport;
	ret = hns_roce_create_qp_cmd(context, qp, &attrx, &cmd, &resp);
	if (ret) {
		roce_err("create qp cmd failed! ret %d", ret);
		goto err_deinit_lock;
	}

	if (ai_attr->qp_mode == HNS_ROCE_QP_AI_MODE_GDR_ASYN) {
		hns_roce_u_v2_sq_init_nop_wqe(qp);
	}

	hns_roce_record_qp_attr(qp, hr_dev, context, &attrx, &resp);
	hns_roce_store_qp_ai_attr(context, qp, ai_attr, ai_resp);

	return &qp->verbs_qp.qp;

err_deinit_lock:
	hns_roce_deinit_sq_rq_lock(qp);

err_free_buf:
	hns_roce_free_qp_buf(qp, context);

err_free_obj:
	hns_roce_ai_free_buf(ai_attr->ai_op_support, qp);
	qp = NULL;
	return NULL;
}

static int hns_roce_init_cq_ctx(struct hns_roce_context *hr_ctx, struct hns_roce_cq *cq, int cqe)
{
    cq->cons_index = 0;
    cq->cqe_size = hr_ctx->cqe_size;
    cq->cq_depth = cqe;
    if (pthread_spin_init(&cq->hr_lock.lock, PTHREAD_PROCESS_PRIVATE)) {
        roce_err("pthread spin lock init failed");
        return -1;
    }

    list_head_init(&cq->list_sq);
    list_head_init(&cq->list_rq);

    if (hns_roce_alloc_cq_buf(cq)) {
        roce_err("alloc cq buf failed");
        goto err_destroy_lock;
    }

    cq->db = hns_roce_alloc_db(hr_ctx, HNS_ROCE_CQ_TYPE_DB, cq->buf_type, &cq->buf);
    if (cq->db == NULL) {
        roce_err("alloc cq db buffer failed!");
        goto err_free_buf;
    }

    return 0;

err_free_buf:
    hns_roce_free_buf(&cq->buf);

err_destroy_lock:
    pthread_spin_destroy(&cq->hr_lock.lock);

    return -1;
}

static void hns_roce_deinit_cq_ctx(struct hns_roce_context *hr_ctx, struct hns_roce_cq *cq)
{
    hns_roce_free_db(hr_ctx, cq->db, HNS_ROCE_CQ_TYPE_DB);
    hns_roce_free_buf(&cq->buf);
    pthread_spin_destroy(&cq->hr_lock.lock);
}

static int hns_roce_verify_cq(int *cqe, struct hns_roce_context *context)
{
	if (*cqe < 1 || *cqe > context->max_cqe)
		return -EINVAL;

	if (*cqe < HNS_ROCE_MIN_CQE_NUM) {
		roce_err("cqe = %d, less than minimum CQE number.\n", *cqe);
		*cqe = HNS_ROCE_MIN_CQE_NUM;
	}

	return 0;
}

struct ibv_cq *hns_roce_u_create_ext_cq(struct ibv_context *context, int cqe, void *cq_context,
    struct ibv_comp_channel *channel, int comp_vector, struct rdma_lite_device_cq_init_attr *attr)
{
    struct hns_roce_create_ai_cq cmd = {0};
    struct hns_roce_create_cq_resp resp = {0};
    struct hns_roce_context *ctx = to_hr_ctx(context);
    struct hns_roce_cq *cq = NULL;
    unsigned int length;
    int ret;

    if (ctx->ai_ctx.enable != HNS_ROCE_AI_ENABLE) {
        roce_event("not support create ai cq.");
        return NULL;
    }

    if (hns_roce_verify_cq(&cqe, ctx)) {
        roce_err("verify cq attr failed");
        return NULL;
    }

    ret = hns_roce_ai_alloc_buf(&cq, &length, sizeof(*cq), HNS_HW_PAGE_SIZE, ctx->dev_id,
        attr->ai_op_support, attr->grp_id);
    if (ret || !cq) {
        roce_err("hns_roce_ai_alloc_buf cq failed! ret = %d", ret);
        return NULL;
    }

    cq->dev_id = ctx->dev_id;
    cq->ai_op_support = attr->ai_op_support;
    cq->cq_cstm_flag = attr->cq_cstm_flag;
    if (attr->cq_type == HNS_ROCE_QP_AI_MODE_OP && attr->lite_op_supported) {
        cq->buf_type = HNS_ROCE_BUF_TYPE_LITE_ALIGN_4KB;
        if (attr->mem_align == LITE_ALIGN_2MB) {
            cq->buf_type = HNS_ROCE_BUF_TYPE_LITE_ALIGN_2MB;
            cq->buf.mem_idx = attr->mem_idx;
            cq->buf.mem_align = attr->mem_align;
        }
    } else if (attr->ai_op_support) {
        cq->buf_type = HNS_ROCE_BUF_TYPE_AI_ALIGN_4KB;
        cq->buf.grp_id = attr->grp_id;
        cq->buf.ai_op_support = attr->ai_op_support;
    }
    ret = hns_roce_init_cq_ctx(ctx, cq, cqe);
    if (ret) {
        roce_err("init cq ctx failed, ret %d", ret);
        goto err;
    }

    cmd.buf_addr = (uintptr_t)(cq->buf.buf + cq->buf.offset);
    cmd.u_cqe_size = cq->cqe_size;
    cmd.db_addr = (uintptr_t) cq->db;
    cmd.cq_mode = 1;
    cmd.partid = attr->part_id;
    ret = ibv_cmd_create_cq(context, cqe, channel, comp_vector, &cq->verbs_cq.cq, &cmd.ibv_cmd, sizeof(cmd),
                            &resp.ibv_resp, sizeof(resp));
    if (ret) {
        roce_err("ibv_cmd_create_cq failed, ret %d", ret);
        goto err_deinit_ctx;
    }

    cq->cqn = resp.cqn;
    cq->cq_depth = cqe;
    cq->flags = resp.cap_flags;

    cq->arm_db = cq->db;
    cq->arm_sn = 1;
    *(cq->db) = 0;
    *(cq->arm_db) = 0;

    return &cq->verbs_cq.cq;

err_deinit_ctx:
    hns_roce_deinit_cq_ctx(ctx, cq);

err:
    hns_roce_ai_free_buf(attr->ai_op_support, cq);
    cq = NULL;
    return NULL;
}

#define HR_IBV_OPC_MAP(ib_key, hr_key) \
    [IBV_WR_ ## ib_key] = HNS_ROCE_WQE_OP_ ## hr_key

#define HR_PRIV_OPC_MAP(hr_key) \
    [HNS_ROCE_WR_ ## hr_key] = HNS_ROCE_WQE_OP_ ## hr_key

static const uint32_t hns_roce_op_code[] = {
    HR_IBV_OPC_MAP(RDMA_WRITE, RDMA_WRITE),
    HR_IBV_OPC_MAP(RDMA_WRITE_WITH_IMM, RDMA_WRITE_WITH_IMM),
    HR_IBV_OPC_MAP(SEND, SEND),
    HR_IBV_OPC_MAP(SEND_WITH_IMM, SEND_WITH_IMM),
    HR_IBV_OPC_MAP(RDMA_READ, RDMA_READ),
    HR_IBV_OPC_MAP(ATOMIC_CMP_AND_SWP, ATOMIC_COM_AND_SWAP),
    HR_PRIV_OPC_MAP(NOP),
    HR_PRIV_OPC_MAP(REDUCE_WRITE),
    HR_PRIV_OPC_MAP(REDUCE_WRITE_NOTIFY),
    HR_PRIV_OPC_MAP(WRITE_NOTIFY),
    HR_PRIV_OPC_MAP(ATOMIC_WRITE),
};

static inline uint32_t to_hr_opcode(uint32_t ib_opcode)
{
    if (ib_opcode >= ARRAY_SIZE(hns_roce_op_code))
        return HNS_ROCE_WQE_OP_MASK;

    return hns_roce_op_code[ib_opcode];
}

static int check_qp_send(struct hns_roce_qp *qp)
{
    struct ibv_qp *ibvqp = &qp->verbs_qp.qp;

    if (unlikely(ibvqp->qp_type != IBV_QPT_RC &&
        ibvqp->qp_type != IBV_QPT_UD)) {
        roce_err("unsupported qp type, qp_type = %d.", ibvqp->qp_type);
        return -EINVAL;
    }

    /* check that state is OK to post receive */
    if (ibvqp->state == IBV_QPS_RESET || ibvqp->state == IBV_QPS_INIT ||
        ibvqp->state == IBV_QPS_RTR) {
        roce_err("unsupported qp state, state = %d.", ibvqp->state);
        return -EINVAL;
    }

    return 0;
}

static int hns_roce_u_v2_check_wr(struct hns_roce_qp *qp, struct ibv_send_wr *wr, int nreq)
{
    if (hns_roce_v2_wq_overflow(&qp->sq, nreq, to_hr_cq(qp->verbs_qp.qp.send_cq))) {
        roce_warn("wq overflow!");
        return -ENOMEM;
    }

    if ((unsigned int)wr->num_sge > qp->sq.max_gs) {
        roce_err("Num of sge(%d) in wr larger than qp's capability(%d)!",
                 wr->num_sge, qp->sq.max_gs);
        return -EINVAL;
    }

    return 0;
}

static inline void *get_wqe(struct hns_roce_qp *qp, int offset)
{
    return qp->buf.buf + offset;
}

static void *get_send_wqe(struct hns_roce_qp *qp, int n)
{
    return get_wqe(qp, qp->sq.offset + (n << qp->sq.wqe_shift));
}

static __le32 get_immtdata(uint32_t opcode, const struct ibv_send_wr *wr)
{
    switch (opcode) {
        case IBV_WR_SEND_WITH_IMM:
        case IBV_WR_RDMA_WRITE_WITH_IMM:
            return htole32(be32toh(wr->imm_data));
        case HNS_ROCE_WR_WRITE_NOTIFY:
        case HNS_ROCE_WR_REDUCE_WRITE_NOTIFY:
            return htole32((be32toh(wr->imm_data)) & RC_SQ_WQE_BYTE_12_NOTIFY_INFO_M);
        default:
            return 0;
    }

    return 0;
}

static void *get_send_sge_ex(struct hns_roce_qp *qp, int n)
{
    return get_wqe(qp, qp->ex_sge.offset + (n << qp->ex_sge.sge_shift));
}

static void set_data_seg_v2(struct hns_roce_v2_wqe_data_seg *dseg, struct ibv_sge *sg)
{
    dseg->lkey = htole32(sg->lkey);
    dseg->addr = htole64(sg->addr);
    dseg->len = htole32(sg->length);
}

static void set_sge(struct hns_roce_v2_wqe_data_seg *dseg,
                    struct hns_roce_qp *qp, struct ibv_send_wr *wr,
                    struct hns_roce_sge_info *sge_info)
{
    int i;

    sge_info->valid_num = 0;
    sge_info->total_len = 0;

    for (i = 0; i < wr->num_sge; i++)
        if (likely(wr->sg_list[i].length)) {
            sge_info->total_len += wr->sg_list[i].length;
            sge_info->valid_num++;

            if((wr->send_flags & IBV_SEND_INLINE &&
               wr->opcode != IBV_WR_ATOMIC_FETCH_AND_ADD &&
               wr->opcode != IBV_WR_ATOMIC_CMP_AND_SWP) ||
               (wr->opcode == HNS_ROCE_WR_ATOMIC_WRITE))
               continue;

            /* No inner sge in UD wqe */
            if (sge_info->valid_num <= HNS_ROCE_SGE_IN_WQE &&
                qp->verbs_qp.qp.qp_type != IBV_QPT_UD) {
                set_data_seg_v2(dseg, wr->sg_list + i);
                dseg++;
            } else {
                dseg = get_send_sge_ex(qp, sge_info->start_idx & (qp->ex_sge.sge_cnt - 1));
                set_data_seg_v2(dseg, wr->sg_list + i);
                sge_info->start_idx++;
            }
        }
}

static int check_ud_opcode(struct hns_roce_ud_sq_wqe *ud_sq_wqe, const struct ibv_send_wr *wr)
{
    uint32_t ib_op = wr->opcode;

    if (ib_op != IBV_WR_SEND && ib_op != IBV_WR_SEND_WITH_IMM)
        return -EINVAL;

    ud_sq_wqe->immtdata = get_immtdata(ib_op, wr);

    roce_set_field(ud_sq_wqe->rsv_opcode, UD_SQ_WQE_OPCODE_M, UD_SQ_WQE_OPCODE_S, to_hr_opcode(ib_op));

    return 0;
}

static void fill_ud_av(struct hns_roce_ud_sq_wqe *ud_sq_wqe, struct hns_roce_ah *ah)
{
    roce_set_field(ud_sq_wqe->sge_num_pd, UD_SQ_WQE_PD_M, UD_SQ_WQE_PD_S, to_hr_pd(ah->ibv_ah.pd)->pdn);
    roce_set_field(ud_sq_wqe->tclass_vlan, UD_SQ_WQE_TCLASS_M, UD_SQ_WQE_TCLASS_S, ah->av.tclass);
    roce_set_field(ud_sq_wqe->tclass_vlan, UD_SQ_WQE_HOPLIMIT_M, UD_SQ_WQE_HOPLIMIT_S, ah->av.hop_limit);
    roce_set_field(ud_sq_wqe->lbi_flow_label, UD_SQ_WQE_FLOW_LABEL_M, UD_SQ_WQE_FLOW_LABEL_S, ah->av.flowlabel);
    roce_set_field(ud_sq_wqe->udpspn_rsv, UD_SQ_WQE_UDP_SPN_M, UD_SQ_WQE_UDP_SPN_S, ah->av.udp_sport);
    roce_set_field(ud_sq_wqe->lbi_flow_label, UD_SQ_WQE_SL_M, UD_SQ_WQE_SL_S, (ah->av.sl & 0x7));
    memcpy(ud_sq_wqe->dmac, ah->av.mac, ETH_ALEN);
    ud_sq_wqe->sgid_index = ah->av.gid_index;
    memcpy(ud_sq_wqe->dgid, ah->av.dgid, HNS_ROCE_GID_SIZE);
}

static void fill_ud_data_seg(struct hns_roce_ud_sq_wqe *ud_sq_wqe,
    struct hns_roce_qp *qp, struct ibv_send_wr *wr,
    struct hns_roce_sge_info *sge_info)
{
    roce_set_field(ud_sq_wqe->rsv_msg_start_sge_idx,
                   UD_SQ_WQE_MSG_START_SGE_IDX_M,
                   UD_SQ_WQE_MSG_START_SGE_IDX_S,
                   sge_info->start_idx & (qp->ex_sge.sge_cnt - 1));

    set_sge((struct hns_roce_v2_wqe_data_seg *)ud_sq_wqe, qp, wr, sge_info);

    ud_sq_wqe->msg_len = htole32(sge_info->total_len);

    roce_set_field(ud_sq_wqe->sge_num_pd, UD_SQ_WQE_SGE_NUM_M,
                   UD_SQ_WQE_SGE_NUM_S, sge_info->valid_num);
}

static void hns_roce_u_v2_sq_init_nop_wqe(struct hns_roce_qp *qp)
{
    struct hns_roce_rc_sq_wqe *wqe;
    unsigned int wqe_idx;

    for (wqe_idx = 0; wqe_idx < qp->sq.wqe_cnt; wqe_idx++) {
        wqe = get_send_wqe(qp, wqe_idx);

        memset(wqe, 0, sizeof(struct hns_roce_rc_sq_wqe));
        roce_set_field(wqe->byte_4, RC_SQ_WQE_BYTE_4_OPCODE_M,
                       RC_SQ_WQE_BYTE_4_OPCODE_S, HNS_ROCE_WQE_OP_NOP);
    }
}

static int hns_roce_u_v2_set_ud_wqe(void *wqe, struct hns_roce_qp *qp, struct ibv_send_wr *wr,
                                    int nreq, struct hns_roce_sge_info *sge_info)
{
    int ret;
    struct hns_roce_ah *ah = to_hr_ah(wr->wr.ud.ah);
    struct hns_roce_ud_sq_wqe *ud_sq_wqe = wqe;

    memset(ud_sq_wqe, 0, sizeof(*ud_sq_wqe));

    roce_set_bit(ud_sq_wqe->rsv_opcode, UD_SQ_WQE_CQE_S, !!(wr->send_flags & IBV_SEND_SIGNALED));
    roce_set_bit(ud_sq_wqe->rsv_opcode, UD_SQ_WQE_SE_S, !!(wr->send_flags & IBV_SEND_SOLICITED));

    ret = check_ud_opcode(ud_sq_wqe, wr);
    if (ret) {
        roce_err("unsupported opcode, opcode = %d.", wr->opcode);
        return ret;
    }

    ud_sq_wqe->qkey = htole32(wr->wr.ud.remote_qkey & 0x80000000 ? qp->qkey : wr->wr.ud.remote_qkey);
    roce_set_field(ud_sq_wqe->rsv_dqpn, UD_SQ_WQE_DQPN_M, UD_SQ_WQE_DQPN_S, htole32(wr->wr.ud.remote_qpn));
    fill_ud_av(ud_sq_wqe, ah);
    fill_ud_data_seg(ud_sq_wqe, qp, wr, sge_info);

    /*
     * The pipeline can sequentially post all valid WQEs in wq buf,
     * including those new WQEs waiting for doorbell to update the PI again.
     * Therefore, the valid bit of WQE MUST be updated after all of fields
     * and extSGEs have been written into DDR instead of cache.
     */
    if (qp->flags & HNS_ROCE_QP_CAP_OWNER_DB)
        udma_to_device_barrier();

    roce_set_bit(ud_sq_wqe->rsv_opcode, UD_SQ_WQE_OWNER_S, ~(((qp->sq.head + nreq) >> qp->sq.shift) & 0x1));

    return 0;
}

static void set_reduce_op(struct hns_roce_rc_sq_wqe *rc_sq_wqe, const struct ibv_post_send_ext_attr *ext_attr)
{
	roce_set_field(rc_sq_wqe->byte_20, RC_SQ_WQE_BYTE_20_REDUCE_TYPE_M, RC_SQ_WQE_BYTE_20_REDUCE_TYPE_S,
				   ext_attr->reduce_type & INLINE_REDUCE_TYPE_MASK);
	roce_set_field(rc_sq_wqe->byte_20, RC_SQ_WQE_BYTE_20_REDUCE_OP_M, RC_SQ_WQE_BYTE_20_REDUCE_OP_S,
				   ext_attr->reduce_op & INLINE_REDUCE_OP_MASK);
}

static int check_rc_opcode(struct hns_roce_rc_sq_wqe *rc_sq_wqe, const struct ibv_send_wr *wr,
                           struct ibv_post_send_ext_attr *ext_attr)
{
    uint32_t ib_op = wr->opcode;
    int ret = 0;

    rc_sq_wqe->immtdata = get_immtdata(ib_op, wr);

    switch (ib_op) {
        case HNS_ROCE_WR_REDUCE_WRITE:
        case HNS_ROCE_WR_REDUCE_WRITE_NOTIFY:
            set_reduce_op(rc_sq_wqe, ext_attr);
            rc_sq_wqe->va = htole64(wr->wr.rdma.remote_addr);
            rc_sq_wqe->rkey = htole32(wr->wr.rdma.rkey);
            break;
        case IBV_WR_RDMA_READ:
        case IBV_WR_RDMA_WRITE:
        case HNS_ROCE_WR_ATOMIC_WRITE:
        case IBV_WR_RDMA_WRITE_WITH_IMM:
        case HNS_ROCE_WR_WRITE_NOTIFY:
            rc_sq_wqe->va = htole64(wr->wr.rdma.remote_addr);
            rc_sq_wqe->rkey = htole32(wr->wr.rdma.rkey);
            break;
        case IBV_WR_SEND:
        case IBV_WR_SEND_WITH_IMM:
        case HNS_ROCE_WR_NOP:
            break;
        default:
            ret = -EINVAL;
            break;
    }

    roce_set_field(rc_sq_wqe->byte_4, RC_SQ_WQE_BYTE_4_OPCODE_M,
                   RC_SQ_WQE_BYTE_4_OPCODE_S, to_hr_opcode(ib_op));
    roce_set_bit(rc_sq_wqe->byte_4, RC_SQ_WQE_BYTE_4_CQE_S,
                   (wr->send_flags & IBV_SEND_SIGNALED) ? 1 : 0);
    roce_set_bit(rc_sq_wqe->byte_4, RC_SQ_WQE_BYTE_4_FENCE_S,
                   (wr->send_flags & IBV_SEND_FENCE) ? 1 : 0);
    roce_set_bit(rc_sq_wqe->byte_4, RC_SQ_WQE_BYTE_4_SE_S,
                   (wr->send_flags & IBV_SEND_SOLICITED) ? 1 : 0);

    return ret;
}

static int set_atom_write_seg(const struct ibv_send_wr *wr, struct hns_roce_rc_sq_wqe *rc_sq_wqe,
                              struct hns_roce_sge_info *sge_info)
{
    void *dseg = (void *)(rc_sq_wqe + 1);
    uint32_t imm_data;

    // strict maximum support atomic write data to 4B, default capability supports 8B
    if (sge_info->total_len > sizeof(uint32_t)) {
        roce_err("total_len:%u exceeds %zu, disallow to atomic write", sge_info->total_len, sizeof(uint32_t));
        return -EINVAL;
    }

    roce_set_bit(rc_sq_wqe->byte_4, RC_SQ_WQE_BYTE_4_INLINE_S, 1);
    roce_set_bit(rc_sq_wqe->byte_20, RC_SQ_WQE_BYTE_20_INL_TYPE_S, 0);

    // atomic write data use 4B in dseg->len field(byte_36)
    imm_data = be32toh(wr->imm_data);
    memcpy(dseg, (void *)(uintptr_t)(&imm_data), sizeof(uint32_t));
    return 0;
}

static int hns_roce_u_v2_set_rc_wqe(void *wqe, struct hns_roce_qp *qp,
                                    struct ibv_send_wr *wr, struct ibv_post_send_ext_attr *ext_attr, int nreq,
                                    struct hns_roce_sge_info *sge_info)
{
    struct hns_roce_rc_sq_wqe *rc_sq_wqe = wqe;
    struct hns_roce_v2_wqe_data_seg *dseg;
    int ret;

    memset(rc_sq_wqe, 0, sizeof(struct hns_roce_rc_sq_wqe));

    ret = check_rc_opcode(rc_sq_wqe, wr, ext_attr);
    if (ret) {
        roce_err("unsupported opcode, opcode = %d.\n", wr->opcode);
        return ret;
    }

    roce_set_field(rc_sq_wqe->byte_20,
                   RC_SQ_WQE_BYTE_20_MSG_START_SGE_IDX_M,
                   RC_SQ_WQE_BYTE_20_MSG_START_SGE_IDX_S,
                   sge_info->start_idx & (qp->ex_sge.sge_cnt - 1));
    wqe += sizeof(struct hns_roce_rc_sq_wqe);
    dseg = wqe;
    set_sge(dseg, qp, wr, sge_info);
    rc_sq_wqe->msg_len = htole32(sge_info->total_len);

    roce_set_field(rc_sq_wqe->byte_16, RC_SQ_WQE_BYTE_16_SGE_NUM_M,
                   RC_SQ_WQE_BYTE_16_SGE_NUM_S, sge_info->valid_num);

    if (wr->opcode == HNS_ROCE_WR_ATOMIC_WRITE) {
        ret = set_atom_write_seg(wr, rc_sq_wqe, sge_info);
        if (ret != 0) {
            roce_err("set_atom_write_seg failed, ret:%d", ret);
            return ret;
        }
    }

    if (qp->flags & HNS_ROCE_QP_CAP_OWNER_DB)
        udma_to_device_barrier();

    roce_set_bit(rc_sq_wqe->byte_4, RC_SQ_WQE_BYTE_4_OWNER_S,
                 ~(((qp->sq.head + nreq) >> qp->sq.shift) & 0x1));

    return 0;
}

static int hns_roce_u_v2_set_wqe(void *wqe, struct hns_roce_qp *qp,
                                 struct ibv_send_wr *wr, struct ibv_post_send_ext_attr *ext_attr, int nreq,
                                 struct hns_roce_sge_info *sge_info)
{
    int ret = 0;

    switch (qp->verbs_qp.qp.qp_type) {
        case IBV_QPT_UD:
            ret = hns_roce_u_v2_set_ud_wqe(wqe, qp, wr, nreq, sge_info);
            break;
        case IBV_QPT_RC:
            ret = hns_roce_u_v2_set_rc_wqe(wqe, qp, wr, ext_attr, nreq, sge_info);
            break;
        default:
            roce_err("Not supported qp type %d", qp->verbs_qp.qp.qp_type);
            return -EINVAL;
    }

    return ret;
}

int hns_roce_u_v2_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
				   int attr_mask);
static void hns_roce_u_v2_handle_err_qp(struct ibv_qp *ibvqp)
{
    int attr_mask;
    struct ibv_qp_attr attr;

    if (ibvqp->state == IBV_QPS_ERR) {
        attr_mask = IBV_QP_STATE;
        attr.qp_state = IBV_QPS_ERR;

        /* Exception handling here, no need to return val */
        if (hns_roce_u_v2_modify_qp(ibvqp, &attr, attr_mask)) {
            roce_err("modify qp failed!");
        }
    }
}

#define DB_SL_OFFSET            48 /* server level offset */
#define DB_PI_OFFSET            32 /* producer index offset */
#define DB_QPN_OFFSET           24 /* QPN offset */

static void hns_roce_u_v2_get_ai_rsp(struct hns_roce_qp *qp, unsigned int wqe_idx,
                                     struct ibv_post_send_ext_resp *ext_resp)
{
    unsigned int pi = qp->sq.head & ((qp->sq.wqe_cnt << 1) - 1);
    if (qp->qp_mode == HNS_ROCE_QP_AI_MODE_OP ||
               qp->qp_mode == HNS_ROCE_QP_AI_MODE_GDR_ASYN) {
        ext_resp->db_info = ((unsigned long)qp->sl << DB_SL_OFFSET) |
                           ((unsigned long)pi << DB_PI_OFFSET) |
                           (0 << DB_QPN_OFFSET) | qp->verbs_qp.qp.qp_num;
    }
}

int hns_roce_u_ext_post_send(struct ibv_qp *ibvqp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr,
    struct ibv_post_send_ext_attr *ext_attr, struct ibv_post_send_ext_resp *ext_resp)
{
    struct hns_roce_qp *qp = to_hr_qp(ibvqp);
    struct hns_roce_sge_info sge_info = {0};
    struct hns_roce_context *ctx = NULL;
    unsigned int wqe_idx = 0;
    void *wqe = NULL;
    int nreq = 0;
    int ret;

    // skip to check ctx when ai_op_support set, to avoid using ctx
    if (qp->buf.ai_op_support == 0) {
        ctx = to_hr_ctx(ibvqp->context);
        if (ctx->ai_ctx.enable != HNS_ROCE_AI_ENABLE) {
            roce_event("not support ai port send.");
            return -EINVAL;
        }
    }

    ret = check_qp_send(qp);
    if (ret) {
        *bad_wr = wr;
        return ret;
    }

    pthread_spin_lock(&qp->sq.hr_lock.lock);
    sge_info.start_idx = qp->next_sge; /* start index of extend sge */

    for (nreq = 0; wr; ++nreq, wr = wr->next) {
        ret = hns_roce_u_v2_check_wr(qp, wr, nreq);
        if (ret) {
            *bad_wr = wr;
            goto out;
        }

        wqe_idx = (qp->sq.head + (unsigned int)nreq) & (qp->sq.wqe_cnt - 1);
        wqe = get_send_wqe(qp, wqe_idx);
        qp->sq.wrid[wqe_idx] = wr->wr_id;

        ret = hns_roce_u_v2_set_wqe(wqe, qp, wr, ext_attr, nreq, &sge_info);
        if (ret) {
            *bad_wr = wr;
            goto out;
        }
    }

out:
    qp->sq.head += nreq;
    qp->next_sge = sge_info.start_idx;
    if (likely(nreq) && qp->qp_mode == HNS_ROCE_QP_AI_MODE_NOR) {
        udma_to_device_barrier();
        hns_roce_update_sq_db(ctx, qp);
        // skip to update sdb when ai_op_support not set, to avoid using db
        if (qp->buf.ai_op_support == 0) {
            if (qp->flags & HNS_ROCE_QP_CAP_SQ_RECORD_DB) {
                *qp->sdb = qp->sq.head & 0xffff;
            }
        }
    }

    pthread_spin_unlock(&qp->sq.hr_lock.lock);

    // skip to handle err qp when ai_op_support not set, to avoid using cq
    if (qp->buf.ai_op_support == 0) {
        hns_roce_u_v2_handle_err_qp(ibvqp);
    }

    if (qp->qp_mode != HNS_ROCE_QP_AI_MODE_NOR) {
        hns_roce_u_v2_get_ai_rsp(qp, wqe_idx, ext_resp);
    }

    return ret;
}

static int mmap_notify_check(struct hns_roce_context *ctx, int type, int partid, struct roce_notify_attr **notify_attr)
{
    if (ctx->ai_ctx.enable != HNS_ROCE_AI_ENABLE) {
        roce_event("not support mmap ai notify.");
        return -EINVAL;
    }

    if ((int32_t)partid > MAX_NOTIFY_ATTR_NUM) {
        roce_err("partid(%d) invalid, max %d.", partid, MAX_NOTIFY_ATTR_NUM);
        return -EINVAL;
    }

    if (type == HNS_ROCE_AI_NOTIFY) {
        *notify_attr = &ctx->ai_ctx.notify_cap.notify_attr[partid];
    } else if (type == HNS_ROCE_AI_CNTR_NOTIFY) {
        *notify_attr = &ctx->ai_ctx.notify_cap.cntr_notify_attr[partid];
    } else {
        roce_err("invalid notify type(%d).", type);
        return -EINVAL;
    }

    if ((*notify_attr)->enable == 0) {
        roce_err("notify not enable.");
        return -EINVAL;
    }

    return 0;
}

static int hns_roce_u_mmap_ai_notify(struct ibv_context *context, unsigned long long *vaddr, size_t total_len,
    int type)
{
    int ret;
    struct hns_roce_context *ctx = to_hr_ctx(context);
    struct roce_notify_attr *notify_attr = NULL;
    void *notify_vaddr = NULL;
    void *mmap_addr;
    int partid = 0;

    if (vaddr == NULL) {
        roce_err("vaddr null.");
        return -EINVAL;
    }

    partid = ctx->ai_ctx.func_cap.die_id;
    ret = mmap_notify_check(ctx, type, partid, &notify_attr);
    if (ret) {
        roce_err("mmap notify check failed.");
        return ret;
    }

    if (total_len > notify_attr->total_len) {
        roce_err("invalid len(%ld), max_len(%ld).", total_len, notify_attr->total_len);
        return -EINVAL;
    }

    if (*vaddr != 0) {
        notify_vaddr = (void*)*vaddr;
    }

    mmap_addr = mmap(notify_vaddr, total_len, PROT_READ | PROT_WRITE, MAP_SHARED,
        ctx->ai_ctx.cmd_fd, (notify_attr->mmap_pgoff << PAGE_SHIFT));
    if (mmap_addr == MAP_FAILED) {
        roce_err("mmap notify failed, length %ld!", total_len);
        return errno;
    }

    ret = ibv_dontfork_range(mmap_addr, total_len);
    if (ret) {
        roce_err("ibv_dontfork_range failed, ret %d!", ret);
        munmap(mmap_addr, total_len);
        return ret;
    }

    if (*vaddr == 0) {
        *vaddr = (unsigned long long)mmap_addr;
    }

    return 0;
}

static int hns_roce_get_notify_cap(struct hns_roce_ai_ctx *ai_ctx)
{
    int ret;

    ret = ioctl(ai_ctx->cmd_fd, HNS_ROCE_AI_GET_NOTIFY_CAP, &ai_ctx->notify_cap);
    if (ret) {
        roce_err("ioctl failed, cmd %ld.", HNS_ROCE_AI_GET_NOTIFY_CAP);
        return ret;
    }

    roce_info("get notify cap ok.");

    return 0;
}

static int hns_roce_get_qp_shm_cap(struct hns_roce_ai_ctx *ai_ctx)
{
    int ret;

    ret = ioctl(ai_ctx->cmd_fd, HNS_ROCE_AI_GET_QP_SHM_CAP, &ai_ctx->qp_shm_cap);
    if (ret) {
        roce_err("ioctl failed, cmd %ld.", HNS_ROCE_AI_GET_QP_SHM_CAP);
        return ret;
    }

    roce_info("shm cap: max_sz %ld, mmap_pgoff %lu.", ai_ctx->qp_shm_cap.max_sz, ai_ctx->qp_shm_cap.mmap_pgoff);

    return 0;
}

static int hns_roce_get_func_cap(struct hns_roce_ai_ctx *ai_ctx)
{
    int ret;

    ret = ioctl(ai_ctx->cmd_fd, HNS_ROCE_AI_GET_FUNC_CAP, &ai_ctx->func_cap);
    if (ret) {
        roce_err("ioctl failed, cmd %ld.", HNS_ROCE_AI_GET_FUNC_CAP);
        return ret;
    }

    roce_info("get func cap ok.");

    return 0;
}

struct roce_slog_api_ops g_roce_slog_api_ops = {
    .DlogRecord  = NULL,
    .CheckLogLevel   = NULL
};
static void *g_slog_api_handle = NULL;
static pthread_mutex_t g_slog_api_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_slog_api_refcount;

int hns_roce_open_slog_so(void)
{
    pthread_mutex_lock(&g_slog_api_lock);
    atomic_fetch_add(&g_slog_api_refcount, 1);
    if (g_slog_api_handle == NULL) {
        g_slog_api_handle = dlopen("libslog.so", RTLD_NOW);
        if (g_slog_api_handle != NULL) {
            pthread_mutex_unlock(&g_slog_api_lock);
            return 0;
        }

        atomic_fetch_sub(&g_slog_api_refcount, 1);
        pthread_mutex_unlock(&g_slog_api_lock);
        return -EINVAL;
    }

    pthread_mutex_unlock(&g_slog_api_lock);
    return 0;
}

void hns_roce_close_slog_so(void)
{
    pthread_mutex_lock(&g_slog_api_lock);
    if (atomic_fetch_sub(&g_slog_api_refcount, 1) == 1) {
        if (g_slog_api_handle != NULL) {
            (void)dlclose(g_slog_api_handle);
            g_slog_api_handle = NULL;
        }
    }
    pthread_mutex_unlock(&g_slog_api_lock);

    return;
}

int hns_roce_slog_api_init(void)
{
    g_roce_slog_api_ops.DlogRecord = (void (*)(int moduleId, int level, const char *fmt, ...))
        dlsym(g_slog_api_handle, "DlogRecord");
    if (g_roce_slog_api_ops.DlogRecord == NULL) {
        printf("g_roce_slog_api_ops.DlogRecord is NULL!");
        return -EINVAL;
    }

    g_roce_slog_api_ops.CheckLogLevel = (int (*)(int moduleId, int logLevel))
        dlsym(g_slog_api_handle, "CheckLogLevel");
    if (g_roce_slog_api_ops.CheckLogLevel == NULL) {
        printf("g_roce_slog_api_ops.CheckLogLevel is NULL!");
        return -EINVAL;
    }

    return 0;
}

int hns_roce_init_ai_ctx(struct ibv_context *ibv_ctx)
{
    struct hns_roce_context *context = to_hr_ctx(ibv_ctx);
    struct hns_roce_ai_ctx *ai_ctx = &context->ai_ctx;
	char *devpath;
    int ret;

    ai_ctx->enable = 0;
    if (asprintf(&devpath, "/dev/%s", ibv_ctx->device->name) < 0) {
        roce_err("asprintf failed.");
        return -EINVAL;
    }

    ai_ctx->cmd_fd = open(devpath, O_RDWR);
    if (ai_ctx->cmd_fd < 0) {
        roce_event("warning: ai cdev(%s) not exist.", devpath);
        free(devpath);
        return 0;
    }

    free(devpath);
    ret = hns_roce_get_qp_shm_cap(ai_ctx);
    if (ret) {
        roce_err("get qp shm cap failed, ret %d.", ret);
        close(ai_ctx->cmd_fd);
        return ret;
    }

    ret = hns_roce_get_notify_cap(ai_ctx);
    if (ret) {
        roce_err("get notify cap failed, ret %d.", ret);
        close(ai_ctx->cmd_fd);
        return ret;
    }

    ret = hns_roce_get_func_cap(ai_ctx);
    if (ret) {
        roce_err("get func cap failed, ret %d.", ret);
        close(ai_ctx->cmd_fd);
        return ret;
    }

    ret = hns_roce_init_hal_api();
    if (ret) {
        roce_err("init hal api failed, ret %d.", ret);
        close(ai_ctx->cmd_fd);
        return ret;
    }

    ai_ctx->enable = HNS_ROCE_AI_ENABLE;

    return 0;
}

int roce_mmap_ai_db_reg(struct ibv_context *ibv_ctx, unsigned int tgid)
{
    struct hns_roce_device *hr_dev = to_hr_dev(ibv_ctx->device);
    struct hns_roce_context *context = to_hr_ctx(ibv_ctx);
    struct hns_roce_tgid_va tgid_va = { 0 };
    int ret;

    if (tgid == 0) {
        roce_err("param err, tgid is 0");
        return -EINVAL;
    }

    tgid_va.length = hr_dev->page_size;
    tgid_va.tgid = tgid;
    ret = ioctl(context->ai_ctx.cmd_fd, HNS_ROCE_AI_MMAP_TGID_VA, &tgid_va);
    if (ret != 0) {
        roce_err("ioctl failed, cmd %ld, ret:%d", HNS_ROCE_AI_MMAP_TGID_VA, ret);
        return ret;
    }

    context->tgid = tgid;
    context->tgid_uar = tgid_va.tgid_va;
    return 0;
}

int roce_unmmap_ai_db_reg(struct ibv_context *ibv_ctx)
{
    struct hns_roce_device *hr_dev = to_hr_dev(ibv_ctx->device);
    struct hns_roce_context *context = to_hr_ctx(ibv_ctx);
    struct hns_roce_tgid_va tgid_va = { 0 };
    int ret;

    if (context->tgid == 0) {
        roce_err("param err, tgid is 0");
        return -EINVAL;
    }

    tgid_va.length = hr_dev->page_size;
    tgid_va.tgid = context->tgid;
    tgid_va.tgid_va = context->tgid_uar;
    ret = ioctl(context->ai_ctx.cmd_fd, HNS_ROCE_AI_UNMMAP_TGID_VA, &tgid_va);
    if (ret != 0) {
        roce_err("ioctl failed, cmd %ld, ret:%d", HNS_ROCE_AI_UNMMAP_TGID_VA, ret);
        return ret;
    }

    return 0;
}

void hns_roce_uninit_ai_ctx(struct ibv_context *ibv_ctx)
{
    struct hns_roce_context *context = to_hr_ctx(ibv_ctx);
    struct hns_roce_ai_ctx *ai_ctx = &context->ai_ctx;

    if (ai_ctx->cmd_fd < 0) {
        return;
    }

    hns_roce_deinit_hal_api();
    close(ai_ctx->cmd_fd);
}

int roce_get_qp_data_plane_info(struct ibv_qp *qp, struct hns_roce_qp_data_plane_info *info)
{
    struct hns_roce_qp *hr_qp = NULL;

    if (unlikely(qp == NULL || info == NULL)) {
        roce_err("qp or info is NULL");
        return -EINVAL;
    }

    hr_qp = to_hr_qp(qp);
    if (unlikely(hr_qp->qp_cstm_flag == 0)) {
        roce_warn("qp_cstm_flag is 0, no need to get qp data plane info");
        return 0;
    }

    info->sq.wqn = hr_qp->verbs_qp.qp.qp_num;
    info->sq.buf_addr = (unsigned long long)hr_qp->buf.buf;
    info->sq.wqebb_size = 1U << hr_qp->sq.wqe_shift;
    info->sq.depth = hr_qp->sq.wqe_cnt;
    info->sq.head_addr = (unsigned long long)&hr_qp->sq.head;
    info->sq.tail_addr = (unsigned long long)&hr_qp->sq.tail;
    info->sq.swdb_addr = (unsigned long long)hr_qp->sdb;
    info->sq.db_reg = (unsigned long long)hr_qp->sq.db_reg;

    info->rq.wqn = hr_qp->verbs_qp.qp.qp_num;
    info->rq.buf_addr = (unsigned long long)hr_qp->buf.buf + hr_qp->rq.offset;
    info->rq.wqebb_size = 1U << hr_qp->rq.wqe_shift;
    info->rq.depth = hr_qp->rq.wqe_cnt;
    info->rq.head_addr = (unsigned long long)&hr_qp->rq.head;
    info->rq.tail_addr = (unsigned long long)&hr_qp->rq.tail;
    info->rq.swdb_addr = (unsigned long long)hr_qp->rdb;
    info->rq.db_reg = (unsigned long long)hr_qp->sq.db_reg;

    return 0;
}

static void hns_roce_set_op_qp_resp(struct rdma_lite_device_qp_attr *qp_resp, struct hns_roce_qp *qp, struct hns_roce_context *ctx)
{
    unsigned long page_size = (unsigned long)to_hr_dev(ctx->ibv_ctx.context.device)->page_size;

    qp_resp->max_inline_data = qp->max_inline_data;
    qp_resp->sge.sge_cnt = qp->ex_sge.sge_cnt;
    qp_resp->next_sge = qp->next_sge;
    qp_resp->sge.offset = qp->ex_sge.offset;
    qp_resp->sge.sge_shift = qp->ex_sge.sge_shift;
    qp_resp->qp_info = 0;
    qp_resp->qpn = qp->verbs_qp.qp.qp_num;
    qp_resp->qp_buf.va = (unsigned long long)qp->buf.buf;
    qp_resp->qp_buf.len = qp->buf.length;
    qp_resp->sq.offset = qp->sq.offset;
    qp_resp->rq.offset = qp->rq.offset;
    qp_resp->sq.wrid_len = qp->sq.wqe_cnt * sizeof(uint64_t);
    qp_resp->rq.wrid_len = qp->rq.wqe_cnt * sizeof(uint64_t);
    qp_resp->sq.max_post = qp->sq.max_post;
    qp_resp->rq.max_post = qp->rq.max_post;
    qp_resp->sq.max_gs = qp->sq.max_gs;
    qp_resp->rq.max_gs = qp->rq.max_gs;
    qp_resp->sq.wqe_cnt = qp->sq.wqe_cnt;
    qp_resp->rq.wqe_cnt = qp->rq.wqe_cnt;
    qp_resp->sq.shift = qp->sq.shift;
    qp_resp->sq.wqe_shift = qp->sq.wqe_shift;
    qp_resp->rq.wqe_shift = qp->rq.wqe_shift;
    qp_resp->sq.db_buf.va = (unsigned long long)qp->sdb;
    if (qp->buf.mem_align == LITE_ALIGN_2MB) {
        qp_resp->sq.db_buf.va = (unsigned long long)qp->buf.buf;
    }
    qp_resp->sq.db_buf.len = (unsigned int)page_size;
    qp_resp->rq.db_buf.va = (unsigned long long)qp->rdb;
    if (qp->buf.mem_align == LITE_ALIGN_2MB) {
        qp_resp->rq.db_buf.va = (unsigned long long)qp->buf.buf;
    }
    qp_resp->rq.db_buf.len = (unsigned int)page_size;
}

struct ibv_qp *ibv_exp_create_qp(struct ibv_pd *pd, struct ibv_exp_qp_init_attr *qp_init_attr, struct rdma_lite_device_qp_attr *qp_resp)
{
    struct hns_roce_context *context = to_hr_ctx(pd->context);
    struct ibv_qp_ai_attr ai_attr;
    struct ibv_qp_ai_resp ai_resp;
    struct ibv_qp *ibvqp;

    ai_attr.qp_mode = qp_init_attr->gdr_enable;
    ai_attr.temp_depth = HNS_ROCE_DEFAULT_TEMP_DEPTH;
    ai_attr.lite_op_support = qp_init_attr->lite_op_support;
    ai_attr.ai_op_support = qp_init_attr->ai_op_support;
    ai_attr.grp_id = qp_init_attr->grp_id;
    ai_attr.qp_cstm_flag = qp_init_attr->qp_cstm_flag;
    ai_attr.mem_align = qp_init_attr->mem_align;
    ai_attr.mem_idx = qp_init_attr->mem_idx;
    ai_attr.udp_sport = qp_init_attr->udp_sport;
    ibvqp = hns_roce_u_create_ai_qp(pd, &qp_init_attr->attr, &ai_attr, &ai_resp);
    if (ibvqp == NULL) {
        return NULL;
    }

    if (qp_init_attr->gdr_enable == IBV_QP_AI_MODE_GDR) {
        qp_resp->qp_info = ai_resp.sq_index;
    } else {
        if (qp_init_attr->gdr_enable == IBV_QP_AI_MODE_OP &&
            ai_attr.lite_op_support) {
            hns_roce_set_op_qp_resp(qp_resp, to_hr_qp(ibvqp), context);
        }
        qp_resp->qp_info = ai_resp.db_index;
    }

    return ibvqp;
}

int ibv_exp_query_notify(struct ibv_context *context, unsigned long long *notify_va, unsigned long long *size)
{
    return hns_roce_u_mmap_ai_notify(context, notify_va, *size, 0);
}

int ibv_exp_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr,
                      struct wr_exp_rsp *exp_rsp)
{
    struct ibv_post_send_ext_attr ext_attr = {0};

    return hns_roce_u_ext_post_send(qp, wr, bad_wr, &ext_attr, (struct ibv_post_send_ext_resp *)exp_rsp);
}

int roce_get_cq_data_plane_info(struct ibv_cq *cq, struct hns_roce_cq_data_plane_info *info)
{
    struct hns_roce_context *ctx = NULL;
    struct hns_roce_cq *hr_cq = NULL;

    if (unlikely(cq == NULL || cq->context == NULL || info == NULL)) {
        roce_err("cq or cq->context or info is NULL");
        return -EINVAL;
    }

    hr_cq = to_hr_cq(cq);
    if (unlikely(hr_cq->cq_cstm_flag == 0)) {
        roce_warn("cq_cstm_flag is 0, no need to get cq data plane info");
        return 0;
    }

    ctx = to_hr_ctx(cq->context);
    info->cqn = hr_cq->cqn;
    info->buf_addr = (unsigned long long)hr_cq->buf.buf;
    info->cqe_size = hr_cq->cqe_size;
    info->depth = hr_cq->cq_depth;
    info->head_addr = 0;
    info->tail_addr = (unsigned long long)&hr_cq->cons_index;
    info->swdb_addr = (unsigned long long)hr_cq->db;
    info->db_reg = (unsigned long long)ctx->tgid_uar + ROCEE_VF_DB_CFG0_OFFSET;

    return 0;
}

static void hns_roce_set_op_cq_resp(struct rdma_lite_device_cq_attr *cq_resp, struct hns_roce_cq *cq, struct ibv_context *context)
{
    unsigned long page_size = (unsigned long)to_hr_dev(context->device)->page_size;

    cq_resp->depth = cq->cq_depth;
    cq_resp->flags = cq->flags;
    cq_resp->cqe_size = cq->cqe_size;
    cq_resp->cqn = cq->cqn;
    cq_resp->cq_buf.va = (unsigned long long)cq->buf.buf;
    cq_resp->cq_buf.len = (unsigned int)cq->buf.length;
    cq_resp->swdb_buf.va = (unsigned long long)cq->db;
    if (cq->buf.mem_align == LITE_ALIGN_2MB) {
        cq_resp->swdb_buf.va = (unsigned long long)cq->buf.buf;
    }
    cq_resp->swdb_buf.len = (unsigned int)page_size;
}

struct ibv_cq *ibv_exp_create_cq(struct ibv_context *context, int cqe, void *cq_context,
    struct ibv_comp_channel *channel, int vector, struct rdma_lite_device_cq_init_attr *attr, struct rdma_lite_device_cq_attr *cq_resp)
{
    struct ibv_cq *ibvcq = NULL;

    if (context == NULL) {
        roce_err("ibv_create_ext_cq context is NULL");
        return NULL;
    }

    ibvcq =  hns_roce_u_create_ext_cq(context, cqe, cq_context, channel, vector, attr);
    if (ibvcq == NULL) {
        roce_err("hns_roce_u_create_ext_cq failed\n");
        return NULL;
    }

    if (attr->cq_type == IBV_QP_AI_MODE_OP && attr->lite_op_supported) {
        hns_roce_set_op_cq_resp(cq_resp, to_hr_cq(ibvcq), context);
    }

    return ibvcq;
}

struct ibv_cq *ibv_create_ext_cq(struct ibv_context *context,
					      int cqe, void *cq_context,
					      struct ibv_comp_channel *channel,
					      int comp_vector, int partid)
{
    struct rdma_lite_device_cq_init_attr attr = { 0 };

    if (context == NULL) {
        roce_err("ibv_create_ext_cq context is NULL");
        return NULL;
    }

    attr.part_id = partid;
    return hns_roce_u_create_ext_cq(context, cqe, cq_context, channel, comp_vector, &attr);
}

int ibv_ext_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
				   struct ibv_send_wr **bad_wr, struct ibv_post_send_ext_attr *ext_attr,
				   struct ibv_post_send_ext_resp *ext_resp)
{
    if (qp == NULL || wr == NULL || bad_wr == NULL || ext_attr == NULL || ext_resp == NULL) {
        roce_err("ibv_ext_post_send qp is NULL or wr is NULL or bad_wr or ext_attr or ext_resp is NULL");
        return -EINVAL;
    }

    return hns_roce_u_ext_post_send(qp, wr, bad_wr, ext_attr, ext_resp);
}

struct ibv_mr *ibv_exp_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
                              int access, struct roce_process_sign roce_sign)
{
    struct ibv_mr_ai_attr attr;
    memcpy(&attr, &roce_sign, sizeof(roce_sign));
    return hns_roce_u_reg_ai_mr(pd, addr, length, access, &attr);
}

int roce_set_tsqp_depth(const char *dev_name, unsigned int rdev_index, unsigned int temp_depth,
    unsigned int *qp_num, unsigned int *sq_depth)
{
    if (dev_name == NULL || qp_num == NULL || sq_depth == NULL) {
        roce_err("dev_name[%p] is NULL or qp_num[%p] is NULL or sq_depth[%p] is NULL",
                 dev_name, qp_num, sq_depth);
        return -EINVAL;
    }

    g_temp_depth = temp_depth;
    *qp_num = g_qp_num;
    *sq_depth = g_sq_depth;

    return 0;
}

int roce_get_tsqp_depth(const char *dev_name, unsigned int rdev_index, unsigned int *temp_depth,
    unsigned int *qp_num, unsigned int *sq_depth)
{
    if (dev_name == NULL || qp_num == NULL || temp_depth == NULL || sq_depth ==NULL) {
        roce_err("dev_name[%p] is NULL or qp_num[%p] is NULL or temp_depth[%p] is NULL or sq_depth[%p] is NULL",
                 dev_name, qp_num, temp_depth, sq_depth);
        return -EINVAL;
    }

    *temp_depth = g_temp_depth;
    *qp_num = g_qp_num;
    *sq_depth = g_sq_depth;

    return 0;
}

int roce_get_roce_dev_data(const char *dev_name, struct roce_dev_data *dev_data)
{
    if (dev_name == NULL || dev_data == NULL) {
        roce_err("dev_name[%p] is NULL or dev_data[%p] is NULL", dev_name, dev_data);
        return -EINVAL;
    }

    dev_data->rdev_index = 0;
    return 0;
}

int ibv_ext_query_cap(struct ibv_context *context, uint64_t *ext_cap_flags)
{
    struct hns_roce_context *ctx;

    if (context == NULL || ext_cap_flags == NULL) {
        roce_err("context or ext_cap_flags is NULL");
        return -EINVAL;
    }

    ctx = to_hr_ctx(context);
    *ext_cap_flags = ctx->ai_ctx.func_cap.ext_cap_flags;

    return 0;
}

int ibv_exp_query_device(struct ibv_context *context, struct dev_cap_info *cap)
{
    struct hns_roce_device *hr_dev = NULL;
    struct hns_roce_context *ctx;

    if (context == NULL || cap == NULL) {
        roce_err("context or cap is NULL!");
        return -EINVAL;
    }

    hr_dev = to_hr_dev(context->device);
    ctx = to_hr_ctx(context);

    cap->num_qps = (1 << (ctx->qp_table_shift + HNS_ROCE_QP_TABLE_BITS));
    cap->port_num = 1;
    cap->qp_table_mask = ctx->qp_table_mask;
    cap->qp_table_shift = ctx->qp_table_shift;
    cap->max_qp_wr = ctx->max_qp_wr;
    cap->max_sge = ctx->max_sge;
    cap->page_size = hr_dev->page_size;

    return 0;
}

int hns_roce_u_get_roce_qpc_stat(struct hns_roce_ai_ctx *ai_ctx, struct hns_roce_qpc_stat *qpc_stat)
{
    int ret;

    ret = ioctl(ai_ctx->cmd_fd, HNS_ROCE_AI_GET_QPC_STAT, qpc_stat);
    if (ret != 0) {
        roce_err("ioctl failed, cmd %ld.", HNS_ROCE_AI_GET_QPC_STAT);
        return ret;
    }

    roce_info("get qpc stat ok.");

    return 0;
}

int roce_query_qpc(struct ibv_qp *qp, struct hns_roce_qpc_attr_val *attr_val, unsigned int attr_mask)
{
    struct hns_roce_qpc_attr qpc_attr = {0};
    struct hns_roce_context *ctx;
    int ret;

    if (qp == NULL || attr_val == NULL) {
        roce_err("qp or attr_val is NULL");
        return -EINVAL;
    }

    ctx = to_hr_ctx(qp->context);
    qpc_attr.qpn = qp->qp_num;
    qpc_attr.attr_mask = attr_mask;

    ret = ioctl(ctx->ai_ctx.cmd_fd, HNS_ROCE_AI_QUERY_QPC_ATTR, &qpc_attr);
    if (ret != 0) {
        roce_err("ioctl failed, cmd %ld.", HNS_ROCE_AI_QUERY_QPC_ATTR);
        return ret;
    }

    memcpy(attr_val, &qpc_attr.attr_val, sizeof(struct hns_roce_qpc_attr_val));

    return 0;
}

struct ibv_ah *ibv_exp_create_ah(struct ibv_pd *pd, struct ibv_exp_ah_attr *attrx)
{
    struct hns_roce_ah *roce_ah = NULL;
    struct ibv_ah *ah = NULL;

    if (pd == NULL || attrx == NULL) {
        roce_err("pd or attrx is NULL");
        return NULL;
    }

    ah = hns_roce_u_create_ah(pd, &attrx->attr);
    if (ah == NULL) {
        return NULL;
    }

    // consistent with ibv_create_ah
    ah->context = pd->context;
    ah->pd = pd;

    if (attrx->udp_sport == 0) {
        return ah;
    }

    roce_ah = to_hr_ah(ah);
    roce_ah->av.udp_sport = attrx->udp_sport & 0xFFFF;
    roce_dbg("set udp_sport 0x%x", roce_ah->av.udp_sport);

    return ah;
}

int roce_remap_mr(struct ibv_mr *ibvmr, struct hns_roce_mr_remap_info info[], unsigned int num)
{
    struct hns_roce_mr_remap_attr remap_attr = { 0 };
    struct hns_roce_context *ctx = NULL;
    int ret;

    if (ibvmr == NULL || info == NULL || num == 0 || num > HNS_ROCE_AI_MR_REMAP_NUM_MAX) {
        roce_err("ibvmr or info is NULL or num:%u is out of range [1, %d]", num, HNS_ROCE_AI_MR_REMAP_NUM_MAX);
        return -EINVAL;
    }

    ctx = to_hr_ctx(ibvmr->context);
    memcpy(remap_attr.dev_path, ibvmr->context->device->dev_path, sizeof(remap_attr.dev_path) - 1);
    remap_attr.ibmr_handle = ibvmr->handle;
    remap_attr.num = num;
    memcpy(remap_attr.info, info, sizeof(struct hns_roce_mr_remap_info) * num);

    ret = ioctl(ctx->ai_ctx.cmd_fd, HNS_ROCE_AI_REMAP_MR, &remap_attr);

    return ret;
}

unsigned int roce_get_api_version(void)
{
    return ROCE_API_VERSION;
}
