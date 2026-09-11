/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_DATATYPE_H
#define ASCCOMM_CCU_DATATYPE_H

#include <memory>

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_operator_v1.h"
#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"

namespace asc {
namespace ccu_rep {

class variable;
class address;

class ccu_phy_res {
public:
    ccu_phy_res() = default;
    ~ccu_phy_res() = default;
    void reset(uint16_t id);
    void set_die_id(uint16_t die_id);
    uint16_t id() const;
    uint16_t die_id() const;

private:
    uint16_t die_id_{0};
    uint16_t id_{0};
};

class ccu_vir_res {
public:
    ccu_vir_res(ccu_rep_context* context);
    virtual ~ccu_vir_res() = default;
    void reset(uint16_t id);
    void reset(uint16_t id, uint16_t die_id);
    void set_die_id(uint16_t die_id);
    virtual uint16_t id() const;
    uint16_t die_id() const;

    ccu_rep_context* get_cur_context() { return context_; }

    std::shared_ptr<ccu_phy_res> get_cur_phy_res() { return phy_res_; }

protected:
    std::shared_ptr<ccu_phy_res> phy_res_{nullptr};
    ccu_rep_context* context_{nullptr};
};

class variable : public ccu_vir_res {
public:
    explicit variable(ccu_rep_context* context = nullptr);
    variable(const variable& other);
    void operator=(variable&& other);

    void operator=(uint64_t immediate);
    void operator=(const variable& other);
    void operator=(ccu_arithmetic_operator<variable, variable> op);
    void operator=(ccu_arithmetic_operator<variable, uint16_t> op);
    void operator=(ccu_arithmetic_operator<address, uint16_t> op);
    void operator=(ccu_arithmetic_operator<address, address> op);

    void operator=(ccu_logic_operator<variable, variable> op);
    void operator=(ccu_logic_operator<variable> op);

    void operator=(ccu_shift_operator<variable, variable> op);

    ccu_arithmetic_operator<variable, variable> operator+(const variable& var_b) const;
    ccu_arithmetic_operator<variable, uint16_t> operator+(const uint16_t offset) const;
    ccu_arithmetic_operator<variable, address> operator+(const address& addr_b) const;

    ccu_arithmetic_operator<variable, variable> operator*(const variable& var_b) const;
    ccu_arithmetic_operator<variable, uint16_t> operator*(const uint16_t offset) const;
    ccu_arithmetic_operator<variable, address> operator*(const address& addr_b) const;

    ccu_arithmetic_operator<variable, variable> operator-(const variable& var_b) const;
    ccu_arithmetic_operator<variable, address> operator-(const address& addr_b) const;
    ccu_arithmetic_operator<variable, uint16_t> operator-(const uint16_t offset) const;

    void operator+=(const variable& other);
    void operator+=(const uint16_t immediate);
    void operator+=(const address& addr_b);
    void operator*=(const variable& other);
    void operator*=(const uint16_t immediate);
    void operator*=(const address& addr_b);
    void operator-=(const variable& other);
    void operator-=(const uint16_t immediate);
    void operator-=(const address& addr_b);
    ccu_relational_operator<variable, uint64_t> operator!=(uint64_t immediate) const;
    ccu_relational_operator<variable, uint64_t> operator==(uint64_t immediate) const;
    ccu_relational_operator<variable, uint64_t> operator<=(uint64_t immediate) const;
    ccu_relational_operator<variable, uint64_t> operator>(uint64_t immediate) const;
    ccu_relational_operator<variable, variable> operator<=(const variable& var_b) const;
    ccu_relational_operator<variable, variable> operator>(const variable& var_b) const;

    ccu_logic_operator<variable, variable> operator&(const variable& var_b) const;
    ccu_logic_operator<variable, address> operator&(const address& addr_b) const;
    ccu_logic_operator<variable, variable> operator|(const variable& var_b) const;
    ccu_logic_operator<variable, address> operator|(const address& addr_b) const;
    ccu_logic_operator<variable, variable> operator^(const variable& var_b) const;
    ccu_logic_operator<variable, address> operator^(const address& addr_b) const;
    ccu_logic_operator<variable> operator~() const;

    void operator&=(const variable& other);
    void operator&=(const address& addr_b);
    void operator|=(const variable& other);
    void operator|=(const address& addr_b);
    void operator^=(const variable& other);
    void operator^=(const address& addr_b);

    ccu_shift_operator<variable, variable> operator<<(const variable& other) const;
    void operator<<=(const variable& other) const;
    ccu_shift_operator<variable, variable> operator>>(const variable& other) const;
    void operator>>=(const variable& other) const;

    void var_var_append_to_context(ccu_arithmetic_operator<variable, variable> op);
    void var_immed_append_to_context(ccu_arithmetic_operator<variable, uint16_t> op);
    void addr_immed_append_to_context(ccu_arithmetic_operator<address, uint16_t> op);
    void addr_addr_append_to_context(ccu_arithmetic_operator<address, address> op);

    void var_var_logic_append_to_context(ccu_logic_operator<variable, variable> op);
    void addr_logic_append_to_context(ccu_logic_operator<variable> op);
};

class address : public ccu_vir_res {
public:
    explicit address(ccu_rep_context* context = nullptr);
    address(const address& other);
    address(variable&& other);
    void operator=(address&& other);

    void operator=(uint64_t immediate);
    void operator=(const address& other);
    void operator=(const variable& other);

    void operator=(ccu_arithmetic_operator<variable, address> op);
    void operator=(ccu_arithmetic_operator<address, address> op);
    void operator=(ccu_arithmetic_operator<address, uint16_t> op);
    void operator=(ccu_arithmetic_operator<variable, variable> op);
    void operator=(ccu_arithmetic_operator<variable, uint16_t> op);

    void operator=(ccu_logic_operator<variable, address> op);
    void operator=(ccu_logic_operator<address, address> op);
    void operator=(ccu_logic_operator<variable, variable> op);
    void operator=(ccu_logic_operator<variable> op);
    void operator=(ccu_shift_operator<variable, variable> op);

    ccu_arithmetic_operator<address, uint16_t> operator+(const uint16_t offset) const;
    ccu_arithmetic_operator<variable, address> operator+(const variable& var_b) const;
    ccu_arithmetic_operator<address, address> operator+(const address& addr_b) const;
    ccu_arithmetic_operator<address, address> operator*(const address& var_b) const;
    ccu_arithmetic_operator<variable, address> operator*(const variable& addr_b) const;
    ccu_arithmetic_operator<address, uint16_t> operator*(const uint16_t offset) const;

    ccu_arithmetic_operator<variable, address> operator-(const variable& var_b) const;
    ccu_arithmetic_operator<address, address> operator-(const address& addr_b) const;
    ccu_arithmetic_operator<address, uint16_t> operator-(const uint16_t offset) const;

    void operator+=(const variable& other);
    void operator+=(const uint16_t immediate);
    void operator*=(const variable& other);
    void operator*=(const uint16_t immediate);
    void operator-=(const variable& other);
    void operator-=(const uint16_t immediate);

    ccu_logic_operator<variable, address> operator&(const variable& var_b) const;
    ccu_logic_operator<address, address> operator&(const address& addr_b) const;
    ccu_logic_operator<variable, address> operator|(const variable& var_b) const;
    ccu_logic_operator<address, address> operator|(const address& addr_b) const;
    ccu_logic_operator<variable, address> operator^(const variable& var_b) const;
    ccu_logic_operator<address, address> operator^(const address& addr_b) const;

    ccu_shift_operator<variable, variable> operator<<(const variable& other) const;
    void operator<<=(const variable& other) const;
    ccu_shift_operator<variable, variable> operator>>(const variable& other) const;
    void operator>>=(const variable& other) const;

    void var_addr_append_to_context(ccu_arithmetic_operator<variable, address> op);
    void addr_addr_append_to_context(ccu_arithmetic_operator<address, address> op);
    void var_immed_append_to_context(ccu_arithmetic_operator<variable, uint16_t> op);
    void addr_immed_append_to_context(ccu_arithmetic_operator<address, uint16_t> op);
    void var_var_append_to_context(ccu_arithmetic_operator<variable, variable> op);
};

class mask_signal : public ccu_vir_res {
public:
    explicit mask_signal(ccu_rep_context* context = nullptr);
};

class ccu_buffer : public ccu_vir_res {
public:
    explicit ccu_buffer(ccu_rep_context* context = nullptr);
    uint16_t id() const override;
    static constexpr uint16_t ccubuffer_die_id_bit = 0x8000; // bit15 表示MS所在的IO Die id
};

class ccu_buf : public ccu_vir_res {
public:
    explicit ccu_buf(ccu_rep_context* context = nullptr);
    uint16_t id() const override;
    static constexpr uint16_t ccubuffer_die_id_bit = 0x8000; // bit15 表示MS所在的IO Die id
};

class executor : public ccu_vir_res {
public:
    explicit executor(ccu_rep_context* context = nullptr);
};

class memory {
public:
    memory() = default;
    memory(address addr, variable token) : addr(addr), token(token) {}
    address addr;
    variable token;
};

/*------------------将Memory改为LocalAddr与RemoteAddr------------------------*/
class local_addr {
public:
    local_addr() = default;
    local_addr(address addr, variable token) : addr(addr), token(token) {}

    address addr;
    variable token;
};

class remote_addr {
public:
    remote_addr() = default;
    remote_addr(address addr, variable token) : addr(addr), token(token) {}

    address addr;
    variable token;
};

/*------------------将MaskSignal改为LocalNotify------------------------*/
class local_notify : public ccu_vir_res {
public:
    explicit local_notify(ccu_rep_context* context = nullptr);
};

// CompletedEvent 退化为纯虚拟资源句柄持有者；
// mask 由调用方（Rep / CcuKernel / C API / wrapper 各层）作为独立参数传入，
// 不再绑定到 Event 上。
class completed_event : public ccu_vir_res {
public:
    explicit completed_event(ccu_rep_context* context = nullptr);
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_DATATYPE_H
