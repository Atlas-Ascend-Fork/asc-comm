#ifndef _HNS_ROCE_U_AI_H
#define _HNS_ROCE_U_AI_H
#include <infiniband/driver.h>
#include <infiniband/verbs.h>
#include "rdma_lite_common.h"

/**
 * API VERSION NUMBER combines major version, minor version and patch version, version range form 0x00 to 0xff.
 * example : 0x020103 means version 0x020103, major 0x02, minor 0x01, patch 0x03
 * when delete API, modify API name, should add major version.
 * when add new API, should add minor version.
 * when modify enum para, struct para add patch version. this means when new API compatible with old API
 */
#define ROCE_API_VER_MAJOR 0x0
#define ROCE_API_VER_MINOR 0x0
#define ROCE_API_VER_PATCH 0x1
#define ROCE_API_VERSION ((ROCE_API_VER_MAJOR << 16U) | (ROCE_API_VER_MINOR << 8U) | (ROCE_API_VER_PATCH))

#define HNS_ROCE_AI_SHARE_WQE_ENTRY_SIZE    128
#define HNS_ROCE_AI_SQ_WQE_SIZE             64

#define INLINE_REDUCE_TYPE_SHIFT_OFFSET   25
#define INLIE_REDUCE_OP_SHIFT_OFFSET      29
#define INLINE_REDUCE_OP_MASK                  0x7
#define INLINE_REDUCE_TYPE_MASK                0xf
#define RC_SQ_WQE_BYTE_20_REDUCE_TYPE_S        24

#define RC_SQ_WQE_BYTE_20_REDUCE_TYPE_M \
		(((1UL << 4) - 1) << RC_SQ_WQE_BYTE_20_REDUCE_TYPE_S)
#define RC_SQ_WQE_BYTE_20_REDUCE_OP_S 28
#define RC_SQ_WQE_BYTE_20_REDUCE_OP_M \
		(((1UL << 3) - 1) << RC_SQ_WQE_BYTE_20_REDUCE_OP_S)

#define RC_SQ_WQE_BYTE_12_NOTIFY_INFO_S 0
#define RC_SQ_WQE_BYTE_12_NOTIFY_INFO_M \
	(((1UL << 25) - 1) << RC_SQ_WQE_BYTE_12_NOTIFY_INFO_S)

#define UD_SQ_WQE_OPCODE_S 0
#define UD_SQ_WQE_OPCODE_M GENMASK(4, 0)
#define UD_SQ_WQE_OWNER_S 7
#define UD_SQ_WQE_DQPN_S 0
#define UD_SQ_WQE_DQPN_M GENMASK(23, 0)
#define UD_SQ_WQE_SE_S 11
#define UD_SQ_WQE_CQE_S 8

#define RC_SQ_WQE_BYTE_4_OWNER_S 7
#define RC_SQ_WQE_BYTE_4_SE_S 11
#define RC_SQ_WQE_BYTE_4_FENCE_S 9

#define RC_SQ_WQE_BYTE_16_SGE_NUM_S 24
#define RC_SQ_WQE_BYTE_16_SGE_NUM_M \
	(((1UL << 8) - 1) << RC_SQ_WQE_BYTE_16_SGE_NUM_S)

#define RC_SQ_WQE_BYTE_20_MSG_START_SGE_IDX_S 0
#define RC_SQ_WQE_BYTE_20_MSG_START_SGE_IDX_M \
	(((1UL << 24) - 1) << RC_SQ_WQE_BYTE_20_MSG_START_SGE_IDX_S)

#define RC_SQ_WQE_BYTE_4_OPCODE_S 0
#define RC_SQ_WQE_BYTE_4_OPCODE_M \
	(((1UL << 5) - 1) << RC_SQ_WQE_BYTE_4_OPCODE_S)

#define UD_SQ_WQE_MSG_START_SGE_IDX_S 0
#define UD_SQ_WQE_MSG_START_SGE_IDX_M GENMASK(23, 0)

#define UD_SQ_WQE_VLAN_EN_S 30

#define UD_SQ_WQE_VLAN_S 0
#define UD_SQ_WQE_VLAN_M GENMASK(15, 0)

#define UD_SQ_WQE_SL_S 20
#define UD_SQ_WQE_SL_M GENMASK(23, 20)

#define UD_SQ_WQE_UDP_SPN_S 16
#define UD_SQ_WQE_UDP_SPN_M GENMASK(31, 16)

#define UD_SQ_WQE_FLOW_LABEL_S 0
#define UD_SQ_WQE_FLOW_LABEL_M GENMASK(19, 0)

#define UD_SQ_WQE_HOPLIMIT_S 16
#define UD_SQ_WQE_HOPLIMIT_M GENMASK(23, 16)

#define UD_SQ_WQE_TCLASS_S 24
#define UD_SQ_WQE_TCLASS_M GENMASK(31, 24)

#define UD_SQ_WQE_PD_S 0
#define UD_SQ_WQE_PD_M GENMASK(23, 0)

#define UD_SQ_WQE_SGE_NUM_S 24
#define UD_SQ_WQE_SGE_NUM_M GENMASK(31, 24)

#define RC_SQ_WQE_BYTE_4_CQE_S 8

enum hns_roce_qp_ai_mode {
    HNS_ROCE_QP_AI_MODE_NOR = 0,
    HNS_ROCE_QP_AI_MODE_GDR = 1,
    HNS_ROCE_QP_AI_MODE_OP = 2,
    HNS_ROCE_QP_AI_MODE_GDR_ASYN = 3,
};

enum hns_roce_notify_type {
    HNS_ROCE_AI_NOTIFY = 0,
    HNS_ROCE_AI_CNTR_NOTIFY = 1,
};

enum {
    HNS_ROCE_EXT_CAP_FLAG_WRITE_NOTIFY  = BIT(0),
    HNS_ROCE_EXT_CAP_FLAG_HOST_ACCESS   = BIT(1),
    HNS_ROCE_EXT_CAP_FLAG_MAX   = BIT(2)
};

enum {
    HNS_ROCE_WR_ATOMIC_MASK_COMP_AND_SWAP = 12,
    HNS_ROCE_WR_ATOMIC_MASK_FETCH_AND_ADD,
    HNS_ROCE_WR_ATOMIC_WRITE = 0xf0,
    HNS_ROCE_WR_PERSISTENCE_WRITE,
    HNS_ROCE_WR_WRITE_NOTIFY = 0xf2,
    HNS_ROCE_WR_NOP = 0xf3,
    HNS_ROCE_WR_PERSISTENCE_WRITE_WITH_IMM,
    HNS_ROCE_WR_REDUCE_WRITE,
    HNS_ROCE_WR_REDUCE_WRITE_NOTIFY,
};

struct hns_roce_create_ai_cq {
    struct ibv_create_cq ibv_cmd;
    uint64_t buf_addr;
    uint64_t db_addr;
    uint32_t u_cqe_size;
    uint32_t attr_flags; /* Use enum hns_roce_create_cq_comp_mask */
    uint64_t create_flags; /* Use enum hns_roce_create_cq_create_flags */
    uint8_t poe_channel;
    uint8_t notify_mode;
    uint16_t notify_idx;
    uint16_t reserved[2];
    uint32_t cq_mode;
    uint32_t partid;
};

struct hns_roce_create_ai_qp {
    struct ibv_create_qp    ibv_cmd;
    uint64_t                buf_addr;
    uint64_t                db_addr;
    __u8                    log_sq_bb_count;
    __u8                    log_sq_stride;
    __u8                    sq_no_prefetch;
    __u8                    reserved[5];
    uint64_t                sdb_addr;
    uint64_t                comp_mask;
    uint64_t                create_flags;
    uint64_t                congest_type_flags;
    uint32_t                qp_mode;
    uint32_t                udp_sport;
    uint64_t                share_addr_base;
};

struct hns_roce_create_ai_qp_resp {
    struct ib_uverbs_create_qp_resp base;
    uint64_t               cap_flags;
};

#define HNS_ROCE_AI_CMD_IOCTL_TYPE  'A'

enum {
    HNS_ROCE_GET_QP_SHM_CAP         = 0x1,
    HNS_ROCE_GET_NOTIFY_CAP         = 0x2,
    HNS_ROCE_INIT_QP_SHM_ATTR       = 0x3,
    HNS_ROCE_UNINIT_QP_SHM_ATTR     = 0x4,
    HNS_ROCE_GET_NOTIFY_CTX         = 0x5,
    HNS_ROCE_GET_FUNC_CAP           = 0x6,
    HNS_ROCE_GET_QPC_STAT           = 0x7,
    HNS_ROCE_QUERY_QPC_ATTR         = 0x8,
    HNS_ROCE_MMAP_TGID_VA           = 0x9,
    HNS_ROCE_UNMMAP_TGID_VA         = 0xA,
    HNS_ROCE_REMAP_MR               = 0xB,
};

#define HNS_ROCE_AI_GET_QP_SHM_CAP  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_GET_QP_SHM_CAP, struct hns_roce_qp_shm_cap)
#define HNS_ROCE_AI_GET_NOTIFY_CAP  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_GET_NOTIFY_CAP, struct hns_roce_notify_cap)
#define HNS_ROCE_AI_INIT_QP_SHM_ATTR  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_INIT_QP_SHM_ATTR, struct hns_roce_qp_shm_attr)
#define HNS_ROCE_AI_UNINIT_QP_SHM_ATTR  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_UNINIT_QP_SHM_ATTR, struct hns_roce_qp_shm_attr)
#define HNS_ROCE_AI_GET_FUNC_CAP  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_GET_FUNC_CAP, struct hns_roce_func_cap)
#define HNS_ROCE_AI_GET_QPC_STAT  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_GET_QPC_STAT, struct hns_roce_qpc_stat)
#define HNS_ROCE_AI_QUERY_QPC_ATTR  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_QUERY_QPC_ATTR, struct hns_roce_qpc_attr)
#define HNS_ROCE_AI_MMAP_TGID_VA  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_MMAP_TGID_VA, struct hns_roce_tgid_va)
#define HNS_ROCE_AI_UNMMAP_TGID_VA  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_UNMMAP_TGID_VA, struct hns_roce_tgid_va)
#define HNS_ROCE_AI_REMAP_MR  \
    _IOWR(HNS_ROCE_AI_CMD_IOCTL_TYPE, HNS_ROCE_REMAP_MR, struct hns_roce_mr_remap_attr)

#define RTLD_NOW 0x00002
#define ROCE 29
#define DLOG_DEBUG 0x0      // debug level id
#define DLOG_INFO  0x1      // info level id
#define DLOG_WARN  0x2      // warning level id
#define DLOG_ERROR 0x3      // error level id
#define RUN_LOG_MASK        (0x01000000)

struct roce_hal_api_ops {
	int (*hns_roce_hal_alloc_buf)(void **buf, unsigned int *length, unsigned int size, unsigned int page_size,
		unsigned int dev_id);
	int (*hns_roce_hal_get_dev_id)(unsigned int chip_id, unsigned int die_id, unsigned int *dev_id);
	int (*hns_roce_hal_alloc_ai_buf)(void **buf, unsigned int *length, unsigned int size, unsigned int page_size,
		unsigned int dev_id, unsigned int grp_id);
	int (*hns_roce_hal_free_ai_buf)(void *buf);
};

struct roce_slog_api_ops {
	/* slog ops api */
	void (*DlogRecord)(int moduleId, int level, const char *fmt, ...);
	int (*CheckLogLevel)(int moduleId, int logLevel);
};

extern struct roce_slog_api_ops g_roce_slog_api_ops;
#define dlog_error(moduleId, fmt, ...) \
	do { \
		if ((g_roce_slog_api_ops.DlogRecord) != NULL) { \
			(g_roce_slog_api_ops.DlogRecord)(moduleId, DLOG_ERROR, "[%s:%d]" fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
		} \
	} while(0)

#define dlog_warn(moduleId, fmt, ...) \
	do { \
		if (((g_roce_slog_api_ops.CheckLogLevel) != NULL) && ((g_roce_slog_api_ops.DlogRecord) != NULL)) { \
			if ((g_roce_slog_api_ops.CheckLogLevel)(moduleId, DLOG_WARN) == 1) { \
				(g_roce_slog_api_ops.DlogRecord)(moduleId, DLOG_WARN, "[%s:%d]" fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
			} \
		} \
	} while (0)

#define dlog_info(moduleId, fmt, ...) \
	do { \
		if (((g_roce_slog_api_ops.CheckLogLevel) != NULL) && ((g_roce_slog_api_ops.DlogRecord) != NULL)) { \
			if ((g_roce_slog_api_ops.CheckLogLevel)(moduleId, DLOG_INFO) == 1) { \
			(g_roce_slog_api_ops.DlogRecord)(moduleId, DLOG_INFO, "[%s:%d]" fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
			} \
		} \
	} while (0)

#define dlog_debug(moduleId, fmt, ...) \
	do { \
		if (((g_roce_slog_api_ops.CheckLogLevel) != NULL) && ((g_roce_slog_api_ops.DlogRecord) != NULL)) { \
			if ((g_roce_slog_api_ops.CheckLogLevel)(moduleId, DLOG_DEBUG) == 1) { \
				(g_roce_slog_api_ops.DlogRecord)(moduleId, DLOG_DEBUG, "[%s:%d]" fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
			} \
		} \
	} while (0)

#define dlog_event(moduleId, fmt, ...) \
	do { \
		if((g_roce_slog_api_ops.DlogRecord) != NULL) { \
			(g_roce_slog_api_ops.DlogRecord)(moduleId|RUN_LOG_MASK, DLOG_INFO, "[%s:%d]" fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
		} \
	} while (0)

#define roce_err(fmt, args...)    dlog_error(ROCE, "%s : " fmt, __func__, ##args)
#define roce_warn(fmt, args...)   dlog_warn(ROCE, "%s : " fmt, __func__, ##args)
#define roce_info(fmt, args...)   dlog_info(ROCE, "%s : " fmt, __func__, ##args)
#define roce_dbg(fmt, args...)    dlog_debug(ROCE, "%s : " fmt, __func__, ##args)
#define roce_event(fmt, args...)  dlog_event(ROCE, "%s : " fmt, __func__, ##args)

struct ibv_exp_qp_init_attr {
	struct ibv_qp_init_attr attr;
	int gdr_enable;
	int lite_op_support;
	int mem_align; // 0,1:4KB, 2:2MB
	unsigned int mem_idx;
	unsigned int udp_sport;
	unsigned int ai_op_support;
	unsigned int grp_id;
	unsigned int qp_cstm_flag;
	unsigned int use_resv_mem;
	unsigned int resv_mem_pool_id;
	unsigned int resv[16U];
};

struct wr_exp_rsp {
    unsigned int wqe_index;
    unsigned long db_info;
};


#define AI_MR_ATTR_SIGN_LENGTH	49

struct ibv_mr_ai_attr {
	int tgid;
	unsigned int dev_id;
	unsigned int vf_id;
	char sign[AI_MR_ATTR_SIGN_LENGTH];
	unsigned int rsvd;
};

enum ibv_qp_ai_mode {
	IBV_QP_AI_MODE_NOR = 0,
	IBV_QP_AI_MODE_GDR = 1,
	IBV_QP_AI_MODE_OP = 2,
};

struct ibv_qp_ai_attr {
	int qp_mode;
	unsigned int temp_depth;
	int lite_op_support;
	int mem_align; // 0,1:4KB, 2:2MB
	unsigned int mem_idx;
	unsigned int udp_sport;
	unsigned int ai_op_support;
	unsigned int grp_id;
	unsigned int qp_cstm_flag;
	unsigned int use_resv_mem;
	unsigned int resv_mem_pool_id;
	unsigned int resv[16U];
};

struct ibv_qp_ai_resp {
	unsigned int db_index;
	unsigned int sq_index;
};

struct ibv_post_send_ext_resp {
	unsigned int wqe_index;
	unsigned long db_info;
};

struct ibv_post_send_ext_attr {
	uint8_t reduce_op;
	uint8_t reduce_type;
};

#define PROCESS_PSIZE_LENGTH 49
struct roce_process_sign {
    int tgid;
    unsigned int devid; /* chip_id */
    unsigned int vfid;
    char sign[PROCESS_PSIZE_LENGTH];
};

struct roce_dev_data {
    unsigned int rdev_index;
    unsigned int reserved;
};

struct hns_roce_reg_ai_mr {
	struct ibv_reg_mr ibv_cmd;
	struct ibv_mr_ai_attr ai_attr;
};

struct hns_roce_qp_shm_cap {
	uint32_t	enable;
	uint32_t	rsvd;
	uint64_t	max_sz;
	uint64_t	mmap_pgoff;
};

struct roce_notify_attr {
	uint32_t	enable;
	uint32_t	rsvd;
	uint64_t	total_len;
	uint64_t	mmap_pgoff;
};

#define MAX_NOTIFY_ATTR_NUM 2
struct hns_roce_notify_cap {
	struct roce_notify_attr	notify_attr[MAX_NOTIFY_ATTR_NUM];
	struct roce_notify_attr	cntr_notify_attr[MAX_NOTIFY_ATTR_NUM];
};

struct hns_roce_func_cap {
	uint32_t	vf_id;
	uint32_t	pf_id;
	uint32_t	pf_num;
	uint32_t	total_vf_num;
	uint64_t	ext_cap_flags;
	uint32_t	chip_id;
	uint32_t	die_id;
};

#define INDEX_LEN 11
#define INFO_PAYLOAD_LEN 1000
struct hns_roce_qpc_stat {
	char index[INDEX_LEN];
	char info[INFO_PAYLOAD_LEN];
};

enum hns_roce_ai_qpc_attr_mask {
    HNS_ROCE_AI_QPC_UDPSPN = 1,
};

struct hns_roce_qpc_attr_val {
    unsigned int udp_sport;
};

struct hns_roce_qpc_attr {
    unsigned int qpn;
    struct hns_roce_qpc_attr_val attr_val;
    unsigned int attr_mask;
};

struct hns_roce_tgid_va {
	uint32_t length;
	uint32_t tgid;
	uint64_t tgid_va;
};

struct hns_roce_ai_ctx {
	int				enable;
	int				cmd_fd;
	struct hns_roce_qp_shm_cap	qp_shm_cap;
	struct hns_roce_notify_cap	notify_cap;
	struct hns_roce_func_cap	func_cap;
};

struct hns_roce_qp_shm_attr {
	uint32_t	qpn;
	uint64_t	vaddr;
	uint64_t	size;
	uint64_t	sq_size;
	uint64_t	sqt_size;
	uint64_t	index;
};

struct hns_roce_qp_shm_ctx {
	unsigned int			sqt_depth;
	unsigned int			share_wqe_pi;
	struct hns_roce_qp_shm_attr	shm_attr;
};

struct ibv_exp_ah_attr {
	struct ibv_ah_attr	attr;
	uint32_t			udp_sport;
	uint32_t			resv[4U];
};

struct hns_roce_wq_data_plane_info {
	unsigned int wqn;
	unsigned long long buf_addr;
	unsigned int wqebb_size;
	unsigned int depth;
	unsigned long long head_addr;
	unsigned long long tail_addr;
	unsigned long long swdb_addr;
	unsigned long long db_reg;
};

struct hns_roce_cq_data_plane_info {
	unsigned int cqn;
	unsigned long long buf_addr;
	unsigned int cqe_size;
	unsigned int depth;
	unsigned long long head_addr;
	unsigned long long tail_addr;
	unsigned long long swdb_addr;
	unsigned long long db_reg;
};

struct hns_roce_qp_data_plane_info {
	struct hns_roce_wq_data_plane_info sq;
	struct hns_roce_wq_data_plane_info rq;
};

struct hns_roce_mr_remap_info {
	void *va; /**< starting address need to remap of mr */
	unsigned long long size; /**< size need to remap of mr */
};

#define HNS_ROCE_AI_MR_REMAP_NUM_MAX  128
struct hns_roce_mr_remap_attr {
	char dev_path[IBV_SYSFS_PATH_MAX];
	unsigned int ibmr_handle;
	unsigned int num;
	struct hns_roce_mr_remap_info info[HNS_ROCE_AI_MR_REMAP_NUM_MAX];
};

int hns_roce_init_ai_ctx(struct ibv_context *ibv_ctx);
void hns_roce_uninit_ai_ctx(struct ibv_context *ibv_ctx);

struct ibv_qp *ibv_exp_create_qp(
    struct ibv_pd *pd, struct ibv_exp_qp_init_attr *qp_init_attr, struct rdma_lite_device_qp_attr *qp_resp);
int ibv_exp_query_notify(struct ibv_context *context, unsigned long long *notify_va, unsigned long long *size);
int ibv_exp_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr,
                      struct wr_exp_rsp *exp_rsp);
struct ibv_cq *ibv_create_ext_cq(struct ibv_context *context,
					      int cqe, void *cq_context,
					      struct ibv_comp_channel *channel,
					      int comp_vector, int partid);
int ibv_ext_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
				   struct ibv_send_wr **bad_wr, struct ibv_post_send_ext_attr *ext_attr,
				   struct ibv_post_send_ext_resp *ext_resp);
struct ibv_mr *ibv_exp_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
                              int access, struct roce_process_sign roce_sign);
int roce_set_tsqp_depth(const char *dev_name, unsigned int rdev_index, unsigned int temp_depth,
    unsigned int *qp_num, unsigned int *sq_depth);
int roce_get_tsqp_depth(const char *dev_name, unsigned int rdev_index, unsigned int *temp_depth,
    unsigned int *qp_num, unsigned int *sq_depth);
int roce_get_roce_dev_data(const char *dev_name, struct roce_dev_data *dev_data);
int ibv_ext_query_cap(struct ibv_context *context, uint64_t *ext_cap_flags);
struct ibv_cq *hns_roce_u_create_ext_cq(struct ibv_context *context, int cqe, void *cq_context,
    struct ibv_comp_channel *channel, int comp_vector, struct rdma_lite_device_cq_init_attr *attr);
int hns_roce_u_ext_post_send(struct ibv_qp *ibvqp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr,
    struct ibv_post_send_ext_attr *ext_attr, struct ibv_post_send_ext_resp *ext_resp);
int hns_roce_u_get_roce_qpc_stat(struct hns_roce_ai_ctx *ai_ctx, struct hns_roce_qpc_stat *qpc_stat);
int hns_roce_open_slog_so(void);
void hns_roce_close_slog_so(void);
int hns_roce_slog_api_init(void);
int hns_roce_lite_alloc_buf(void **buf, unsigned int *length, unsigned int size, unsigned int page_size, unsigned int dev_id);
int hns_roce_lite_get_devid(unsigned int chip_id, unsigned int die_id, unsigned int *dev_id);
int ibv_exp_query_device(struct ibv_context *context, struct dev_cap_info *cap);
struct ibv_cq *ibv_exp_create_cq(struct ibv_context *context, int cqe, void *cq_context,
    struct ibv_comp_channel *channel, int vector, struct rdma_lite_device_cq_init_attr *attr, struct rdma_lite_device_cq_attr *cq_resp);
int hns_roce_ai_alloc_buf(void **buf, unsigned int *length, unsigned int size, unsigned int page_size,
	unsigned int dev_id, unsigned int ai_op_support, unsigned int grp_id);
void hns_roce_ai_free_buf(unsigned int ai_op_support, void *buf);
int roce_mmap_ai_db_reg(struct ibv_context *ibv_ctx, unsigned int tgid);
int roce_unmmap_ai_db_reg(struct ibv_context *ibv_ctx);
int roce_query_qpc(struct ibv_qp *qp, struct hns_roce_qpc_attr_val *attr_val, unsigned int attr_mask);
struct ibv_ah *ibv_exp_create_ah(struct ibv_pd *pd, struct ibv_exp_ah_attr *attrx);
int roce_get_cq_data_plane_info(struct ibv_cq *cq, struct hns_roce_cq_data_plane_info *info);
int roce_get_qp_data_plane_info(struct ibv_qp *qp, struct hns_roce_qp_data_plane_info *info);
int roce_remap_mr(struct ibv_mr *ibvmr, struct hns_roce_mr_remap_info info[], unsigned int num);
unsigned int roce_get_api_version(void);
#endif /* _HNS_ROCE_U_AI_H */
