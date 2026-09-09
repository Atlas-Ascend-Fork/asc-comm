/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REP_CTX_H
#define CCU_REP_CTX_H

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>

#include "hcomm/hcomm_ccu_channel.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"

#include "hcomm/common/ccu_common.h"

namespace asc {
constexpr uint16_t ccu_max_channel_num = 16;                    // 最多16条link
constexpr uint16_t invalid_cke_id = 0xFFFF;                     // CKE ID非法值
constexpr uint16_t invalid_value_channelid = 0xFFFF;            // channel id非法值
constexpr uint64_t invalid_value_notifyid = 0xFFFFFFFFFFFFFFFF; // NOTIFY id非法值

enum class ccu_profilin_type { ccu_task_profiling, ccu_waitcke_profiling, ccu_loopgroup_profiling, ccu_map_profiling };

struct ccu_profiling_info {
    std::string name;
    uint8_t type{0};
    uint8_t die_id{0};
    uint8_t mission_id{0};
    uint16_t instr_id{0};
    uint8_t reduce_op_type{0};
    uint8_t input_data_type{0};
    uint8_t output_data_type{0};
    uint64_t data_size{0};
    uint32_t cke_id{0};
    uint32_t mask{0};
    uint16_t channel_id[ccu_max_channel_num];
    uint32_t remote_rank_id[ccu_max_channel_num];
    uint64_t channel_handle[ccu_max_channel_num];

    ccu_profiling_info()
    {
        std::fill(std::begin(channel_id), std::end(channel_id), invalid_value_channelid);
        std::fill(std::begin(remote_rank_id), std::end(remote_rank_id), UINT32_MAX);
        std::fill(std::begin(channel_handle), std::end(channel_handle), invalid_value_notifyid);
    }
};
namespace ccu_rep {

struct loop_group_profiling_info {
    std::vector<ccu_profiling_info> ccu_profiling_infos;
    std::unordered_map<std::shared_ptr<ccu_rep::ccu_rep_base>, uint32_t> load_rep2_arg_idx_map; // loadArg rep -> argIdx
    std::vector<std::shared_ptr<ccu_rep_base>> assign_profiling_reps;                           // assign rep
    std::vector<std::shared_ptr<ccu_rep_base>> lg_profiling_reps;                               // loopgroup rep
};

class ccu_rep_context {
public:
    explicit ccu_rep_context();
    virtual ~ccu_rep_context();

    // 平台层内部使用
    std::shared_ptr<ccu_rep::ccu_rep_block> current_block();
    void set_current_block(std::shared_ptr<ccu_rep::ccu_rep_block> rep_block);
    virtual void append(std::shared_ptr<ccu_rep::ccu_rep_base> rep);
    const std::vector<std::shared_ptr<ccu_rep::ccu_rep_base>>& get_rep_sequence();
    std::shared_ptr<ccu_rep::ccu_rep_base> get_rep_by_instr_id(uint16_t instr_id);
    void dump_represtation();

    void set_die_id(uint32_t die_id);
    uint32_t get_die_id() const;
    void set_mission_id(uint32_t mission_id);
    uint32_t get_mission_id() const;
    void set_mission_key(uint32_t mission_key);
    uint32_t get_mission_key() const;

    // ccu profiling相关接口
    std::vector<ccu_profiling_info>& get_profiling_info();
    ccu_rep::loop_group_profiling_info& get_lg_profiling_info();
    const std::vector<std::shared_ptr<ccu_rep::ccu_rep_base>>& get_waite_cke_profiling_reps() const;
    void collect_profiling_reps(std::shared_ptr<ccu_rep::ccu_rep_base> rep);

    void add_sqe_profiling(const std::string& kernel_name);
    int32_t add_profiling(const std::string& name, uint32_t mask);
    int32_t add_profiling(const ChannelHandle channel, const std::string& name, uint32_t signal_index, uint32_t mask);
    int32_t add_profiling(const ChannelHandle* channels, uint32_t channel_num);
    int32_t add_profiling(
        const ChannelHandle* channels, uint32_t channel_num, int32_t hcomm_data_type, int32_t hcomm_output_data_type,
        int32_t hcomm_op_type);

    void set_dependency_info(uint32_t id, uint32_t mask, const std::shared_ptr<ccu_rep_base>& rep);
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<ccu_rep_base>>> get_dependency_info(uint32_t id);
    void erase_dependency_info(uint32_t id);
    void clear_dependency_info();

public:
    // CCU Profiling相关数据
    ccu_profiling_info ccu_profiling_info_cache;
    std::vector<std::shared_ptr<ccu_rep_base>> all_lg_profiling_reps;   // 当前所有的loopGroup Rep
    loop_group_profiling_info lg_profiling_info;                        // LoopGroup相关profiling缓存信息
    std::vector<std::shared_ptr<ccu_rep_base>> wait_cke_profiling_reps; // waitCKE相关REP缓存
    std::vector<ccu_profiling_info> profiling_info;                     // context全部profiling缓存信息
    // 需要校验返回值是否为nullptr
    ccu_ins_generater_base* get_ins_generator() { return ins_generator_; }

    void set_ins_generater(ccu_ins_generater_base* ins_generater_base) { ins_generator_ = ins_generater_base; }

protected:
    std::set<std::string> registered_loop_;
    ccu_ins_generater_base* ins_generator_{nullptr};
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::vector<std::shared_ptr<ccu_rep_base>>>> dep_info_;

private:
    std::shared_ptr<ccu_rep::ccu_rep_block> active_block_{nullptr};
    std::shared_ptr<ccu_rep::ccu_rep_block> main_block_{nullptr};

    uint32_t die_id_{UINT32_MAX};
    uint32_t mission_id_{UINT32_MAX};
    uint32_t mission_key_{0};
};

}; // namespace ccu_rep
}; // namespace asc

#endif // _CCU_REP_CTX_H
