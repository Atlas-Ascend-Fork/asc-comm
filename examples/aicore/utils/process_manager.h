/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file process_manager.h
 * \brief Ascend C样例共享的多进程管理工具。
 *
 * 样例统一通过二进制直接拉起多个rank进程，本工具封装其中的公共逻辑：
 *   - 按调用方给出的入口函数与参数拉起若干子进程（fork，不exec）；
 *   - 等待全部子进程退出并聚合结果，或超时/收到SIGINT/SIGTERM时终止它们。
 *
 * 本工具只管进程生命周期：命令行解析、rank间通信（root info交换、barrier）以及
 * 临时文件都由调用方负责。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_PROCESS_MANAGER_H
#define ASC_COMM_EXAMPLES_UTILS_PROCESS_MANAGER_H

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace examples {

// args_of：函数类型F -> 参数tuple类型（FunctionTrait惯用法）。
// 用法：std::vector<args_of<decltype(fn)>> ts{{1, false}, {2, true}};
template <class F>
struct function_trait;

template <class R, class... Args>
struct function_trait<R(Args...)> {
    using type = std::tuple<Args...>;
};

template <class F>
using args_of = typename function_trait<F>::type;

// 进程组配置。
struct process_group_options {
    // 整组超时秒数；0表示不限时（默认）。非零时，超时后先向所有子进程发SIGTERM，宽限期后
    // 仍存活的收到SIGKILL，wait_all最终返回false。
    uint32_t timeout_seconds = 0;
    // SIGTERM（超时、terminate_all或Ctrl+C触发的终止）后等待子进程自行退出的宽限秒数。
    uint32_t grace_seconds = 5;
};

// 一个process_group持有一次运行拉起的全部子进程。
//
// 用法：
//   examples::process_group group;
//   if (!group.launch(count, entry_fn, std::move(per_rank_args))) { return 1; }
//   bool pass = group.wait_all();
//
// 同一进程同时只能有一个活跃的process_group（SIGINT/SIGTERM处理器的路由需要），
// 样例场景是main里一个组用到底，不受影响。
class process_group {
public:
    explicit process_group(process_group_options options = {}) : launch_options(options) {}

    process_group(const process_group&) = delete;
    process_group& operator=(const process_group&) = delete;
    process_group(process_group&&) = delete;
    process_group& operator=(process_group&&) = delete;

    ~process_group()
    {
        // 兜底：调用方忘记wait_all/terminate_all时也不留孤儿进程。
        terminate_all();
        if (active_group == this) {
            active_group = nullptr;
        }
    }

    // 拉起count个子进程。第rank个子进程执行 int code = std::apply(entry, per_rank_args[rank])
    // 后以code退出。entry的签名没有任何约定，rank及其在每个rank上的差异由调用方
    // 组织进per_rank_args的各个tuple。
    //
    // 返回false表示拉起失败（fork失败或per_rank_args.size() != count），此时已拉起的
    // 子进程已被回收，直接返回错误即可。
    template <typename Entry, typename... Args>
    bool launch(uint32_t count, Entry&& entry, std::vector<std::tuple<Args...>>&& per_rank_args)
    {
        if (per_rank_args.size() != static_cast<size_t>(count)) {
            return false;
        }
        if (active_group != nullptr) {
            return false;
        }
        install_signal_handlers();

        child_pids.assign(count, -1);
        rank_exit_codes.assign(count, 0);
        for (uint32_t rank = 0; rank < count; ++rank) {
            pid_t pid = fork();
            if (pid < 0) {
                std::fprintf(stderr, "[process_group] fork rank %u failed: %s\n", rank, std::strerror(errno));
                // 回收已拉起的子进程，不留孤儿：0秒宽限直接升级到SIGKILL。
                reap_all(std::chrono::seconds(0));
                return false;
            }
            if (pid == 0) {
                // 子进程：恢复默认信号处理，执行调用方入口后直接退出。
                // _exit不走父进程栈上对象的析构——ACL/HCCL句柄不能被二次析构。
                restore_signal_handlers();
                int code = 0;
                try {
                    code = std::apply(std::forward<Entry>(entry), std::move(per_rank_args[rank]));
                } catch (const std::exception& err) {
                    std::fprintf(stderr, "[process_group] rank %u threw: %s\n", rank, err.what());
                    code = 0xFF;
                } catch (...) {
                    code = 0xFF;
                }
                std::fflush(nullptr);
                _exit(code);
            }
            child_pids[rank] = pid;
        }
        active_group = this;
        return true;
    }

    // 等待全部子进程退出。任一子进程失败不会提前放弃回收其余子进程——半途退出
    // 会留下阻塞在通信域或host barrier上的孤儿进程。
    // 返回true当且仅当所有子进程都正常退出且退出码为0。
    bool wait_all()
    {
        if (child_pids.empty()) {
            return true;
        }
        const auto deadline =
            launch_options.timeout_seconds == 0U ?
                std::chrono::steady_clock::time_point::max() :
                std::chrono::steady_clock::now() + std::chrono::seconds(launch_options.timeout_seconds);
        const bool all_passed = wait_and_reap(deadline);
        interrupted_flag = interrupted_flag || signal_flag != 0;
        return all_passed && !interrupted_flag;
    }

    // 终止全部子进程并回收。SIGINT/SIGTERM到达父进程时自动触发，也可手动调用；
    // 对已退出的进程无副作用，可重复调用。
    void terminate_all()
    {
        if (child_pids.empty()) {
            return;
        }
        signal_all(SIGTERM);
        reap_all(std::chrono::seconds(launch_options.grace_seconds));
    }

    // 各rank的最终状态：正常退出为退出码（0-255），被信号杀死为负值（如-9），
    // 超时被杀同样表现为负值。wait_all/terminate_all完成后有效，未启动的rank为0。
    const std::vector<int>& exit_codes() const { return rank_exit_codes; }

    // Ctrl+C/SIGTERM是否介入过本次运行，用于把"被中断"和"测试失败"区分开。
    bool interrupted() const { return interrupted_flag; }

private:
    static constexpr std::chrono::milliseconds POLL_INTERVAL{50};

    // 每个rank的等待策略，见collect_one：
    enum class wait_mode {
        BLOCKING,     // 无限期阻塞等一个子进程
        NON_BLOCKING, // 只收割已退出的
    };

    // 等待并收割全部子进程。返回true当且仅当全部正常退出且退出码为0。
    // deadline为time_point::max()时表示不限时；两种路径均轮询，以持续检查信号。
    bool wait_and_reap(std::chrono::steady_clock::time_point deadline)
    {
        const bool has_deadline = deadline != std::chrono::steady_clock::time_point::max();
        bool all_passed = true;
        // 每圈循环收割当前已退出的子进程；未到deadline且还有子进程未退就继续等。
        while (has_running_child()) {
            if (signal_flag != 0) {
                interrupted_flag = true;
                terminate_all();
                return false;
            }
            if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
                std::fprintf(
                    stderr, "[process_group] timeout after %u s, terminating all ranks\n",
                    launch_options.timeout_seconds);
                terminate_all();
                return false;
            }
            const bool round_passed = collect_one(wait_mode::NON_BLOCKING);
            if (!round_passed) {
                all_passed = false;
            }
            if (has_running_child()) {
                std::this_thread::sleep_for(POLL_INTERVAL);
            }
        }
        // 终止路径（terminate_all -> reap_all）已写过rank_exit_codes，这里不再覆盖。
        return all_passed;
    }

    // 收割一批已退出的子进程。返回true当且仅当本轮收割到的子进程都正常退出且退出码为0；
    // 没有子进程退出时返回true（不改变结论）。
    bool collect_one(wait_mode mode)
    {
        bool round_passed = true;
        while (true) {
            int status = 0;
            const int options = mode == wait_mode::NON_BLOCKING ? WNOHANG : 0;
            const pid_t pid = waitpid(-1, &status, options);
            if (pid < 0) {
                if (errno == EINTR) {
                    // 被信号打断：回到外层循环重新检查signal_flag与deadline。
                    return true;
                }
                // ECHILD：没有可等待的子进程，正常收尾。
                return round_passed;
            }
            if (pid == 0) {
                // WNOHANG下没有已退出的子进程。
                return round_passed;
            }
            if (!record_exit(pid, status)) {
                round_passed = false;
            }
            if (mode == wait_mode::NON_BLOCKING) {
                continue; // 宽限期轮询路径：继续收割其余已退出的。
            }
            // BLOCKING路径同样继续，直到waitpid返回<=0。
        }
    }

    // 把一个已退出子进程的状态记入rank_exit_codes。返回false表示该子进程异常
    // （非正常退出或退出码非0）。
    bool record_exit(pid_t pid, int status)
    {
        for (size_t i = 0; i < child_pids.size(); ++i) {
            if (child_pids[i] == pid) {
                child_pids[i] = -1;
                if (WIFEXITED(status)) {
                    rank_exit_codes[i] = WEXITSTATUS(status);
                    return WEXITSTATUS(status) == 0;
                }
                rank_exit_codes[i] = WIFSIGNALED(status) ? -WTERMSIG(status) : -1;
                return false;
            }
        }
        return true; // 不是本组的子进程（不应发生），忽略。
    }

    bool has_running_child() const
    {
        for (pid_t pid : child_pids) {
            if (pid > 0) {
                return true;
            }
        }
        return false;
    }

    void signal_all(int sig)
    {
        for (pid_t pid : child_pids) {
            if (pid > 0) {
                kill(pid, sig);
            }
        }
    }

    // 向所有子进程发SIGTERM，等待grace时间后仍存活的收到SIGKILL，然后收割全部。
    // grace为grace为0秒（0秒）时直接升级到SIGKILL，用于Launch失败的清理。
    void reap_all(std::chrono::seconds grace)
    {
        if (!has_running_child()) {
            return;
        }
        signal_all(SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + grace;
        while (has_running_child() && std::chrono::steady_clock::now() < deadline) {
            collect_one(wait_mode::NON_BLOCKING);
            if (has_running_child()) {
                // 轮询间隔为5ms（POLL_INTERVAL的1/10），避免短宽限期下错过窗口。
                std::this_thread::sleep_for(POLL_INTERVAL / 10U);
            }
        }
        if (has_running_child()) {
            signal_all(SIGKILL);
        }
        while (has_running_child()) {
            collect_one(wait_mode::BLOCKING);
        }
    }

    void install_signal_handlers()
    {
        struct sigaction action {};
        action.sa_handler = &process_group::on_signal;
        sigemptyset(&action.sa_mask);
        // SA_RESTART使被信号中断的系统调用自动重启，避免waitpid等阻塞调用返回EINTR。
        action.sa_flags = SA_RESTART;
        sigaction(SIGINT, &action, &previous_int);
        sigaction(SIGTERM, &action, &previous_term);
    }

    void restore_signal_handlers()
    {
        sigaction(SIGINT, &previous_int, nullptr);
        sigaction(SIGTERM, &previous_term, nullptr);
    }

    // 信号处理器只做async-signal-safe的事：写标志 + 向子进程补发SIGTERM。
    // 交互式Ctrl+C时内核会把SIGINT发给整个前台进程组，子进程已各自收到；
    // 这里补发覆盖非交互场景（kill只打父进程）。
    static void on_signal(int sig)
    {
        if (active_group != nullptr) {
            active_group->signal_flag = sig;
            active_group->signal_all(sig);
        }
    }

    process_group_options launch_options;
    std::vector<pid_t> child_pids;
    std::vector<int> rank_exit_codes;
    volatile sig_atomic_t signal_flag = 0;
    bool interrupted_flag = false;
    struct sigaction previous_int {};
    struct sigaction previous_term {};

    static process_group* active_group;
};

// 静态成员定义。header-only下放于命名空间作用域，每个包含本头文件的翻译单元
// 各持有一份（样例均为单可执行文件单翻译单元使用，不构成问题）。
inline process_group* process_group::active_group = nullptr;

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_PROCESS_MANAGER_H
