/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef VECTOR_QUEUE_H
#define VECTOR_QUEUE_H

#include "queue.h"
#include <vector>
#include <algorithm>

namespace Hccl {

template <typename T> class VectorQueue : public Queue<T> {
private:
    std::vector<T> elems_;

public:
    VectorQueue() {
        elems_.reserve(2048);
    }
    class Iterator : public Queue<T>::Iterator {
    private:
        VectorQueue *queue_{nullptr};
        u32          index_ = 0;

    protected:
        void check() override
        {
            if ((this->index_) > queue_->elems_.size()) {
                THROW<InternalException>(StringFormat("VectorQueue<T>::Iterator out of range"));
            }
        }

    public:
        using pointer   = T *;
        using reference = T &;

        Iterator(VectorQueue *queue, u32 index) : queue_(queue), index_(index)
        {
            check();
        }

        ~Iterator() override = default;
        
        reference operator*() const override
        {
            return (this->queue_->elems_[this->index_]);
        }

        pointer operator->() const override
        {
            return &(this->queue_->elems_[this->index_]);
        }

        typename Queue<T>::Iterator &operator++() override
        {
            (this->index_)++;
            check();
            return *this;
        }

        typename Queue<T>::Iterator operator++(int) override
        {
            Iterator temp = *this;
            (this->index_)++;
            check();
            return temp;
        }

        typename Queue<T>::Iterator &operator--() override
        {
            (this->index_)--;
            check();
            return *this;
        }

        typename Queue<T>::Iterator operator--(int) override
        {
            Iterator temp = *this;
            (this->index_)--;
            check();
            return temp;
        }
        // 当前==方法的使用场景为同一队列中，是否为同一个索引的地方。而不是元素值是否相等，也不用于不同队列的比较。
        bool operator==(const typename Queue<T>::Iterator &other) const override
        {
            return this->index_ == static_cast<const Iterator&>(other).index_;
        }
        // 当前!=方法的使用场景为同一队列中，是否为同一个索引的地方。而不是元素值是否相等，也不用于不同队列的比较。
        bool operator!=(const typename Queue<T>::Iterator &other) const override
        {
            return this->index_ != static_cast<const Iterator&>(other).index_;
        }
    };

    void Append(const T &value) override
    {
        elems_.push_back(value);
    }

    void Traverse(std::function<void(const T &)> action) override
    {
        for (const auto &elem : elems_) {
            action(elem);
        }
    }

    size_t Size() const override
    {
        return elems_.size();
    }

    bool IsEmpty() const override
    {
        return elems_.empty();
    }

    bool IsFull() const override
    {
        return false;
    }

    size_t Capacity() const override
    {
        return elems_.capacity();
    }

    std::shared_ptr<typename Queue<T>::Iterator> Find(std::function<bool(const T &)> cond) override
    {
        auto it = std::find_if(elems_.begin(), elems_.end(), cond);
        if (it != elems_.end()) {
            return std::make_shared<Iterator>(this, static_cast<u32>(it - elems_.begin()));
        }
        return std::make_shared<Iterator>(this, static_cast<u32>(it - elems_.begin()));
    }

    std::shared_ptr<typename Queue<T>::Iterator> Begin() override
    {
        return std::make_shared<Iterator>(this, 0);
    }

    std::shared_ptr<typename Queue<T>::Iterator> Tail() override
    {
        return std::make_shared<Iterator>(this, static_cast<u32>(elems_.size() - 1));
    }

    std::shared_ptr<typename Queue<T>::Iterator> End() override
    {
        return std::make_shared<Iterator>(this, static_cast<u32>(elems_.size()));
    }
};

} // namespace Hccl
#endif // VECTOR_QUEUE_H