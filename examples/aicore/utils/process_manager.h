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

// ArgsOf：函数类型F -> 参数tuple类型（FunctionTrait惯用法）。
// 用法：std::vector<ArgsOf<decltype(fn)>> ts{{1, false}, {2, true}};
template <class F>
struct FunctionTrait;

template <class R, class... Args>
struct FunctionTrait<R(Args...)> {
    using type = std::tuple<Args...>;
};

template <class F>
using ArgsOf = typename FunctionTrait<F>::type;

// 进程组配置。
struct ProcessGroupOptions {
    // 整组超时秒数；0表示不限时（默认）。非零时，超时后先向所有子进程发SIGTERM，宽限期后
    // 仍存活的收到SIGKILL，WaitAll最终返回false。
    uint32_t timeoutSeconds = 0;
    // SIGTERM（超时、TerminateAll或Ctrl+C触发的终止）后等待子进程自行退出的宽限秒数。
    uint32_t graceSeconds = 5;
};

// 一个ProcessGroup持有一次运行拉起的全部子进程。
//
// 用法：
//   examples::ProcessGroup group;
//   if (!group.Launch(count, entryFn, std::move(perRankArgs))) { return 1; }
//   bool pass = group.WaitAll();
//
// 同一进程同时只能有一个活跃的ProcessGroup（SIGINT/SIGTERM处理器的路由需要），
// 样例场景是main里一个组用到底，不受影响。
class ProcessGroup {
public:
    explicit ProcessGroup(ProcessGroupOptions options = {}) : options_(options) {}

    ProcessGroup(const ProcessGroup&) = delete;
    ProcessGroup& operator=(const ProcessGroup&) = delete;
    ProcessGroup(ProcessGroup&&) = delete;
    ProcessGroup& operator=(ProcessGroup&&) = delete;

    ~ProcessGroup()
    {
        // 兜底：调用方忘记WaitAll/TerminateAll时也不留孤儿进程。
        TerminateAll();
        if (activeGroup_ == this) {
            activeGroup_ = nullptr;
        }
    }

    // 拉起count个子进程。第rank个子进程执行 int code = std::apply(entry, perRankArgs[rank])
    // 后以code退出。entry的签名没有任何约定，rank及其在每个rank上的差异由调用方
    // 组织进perRankArgs的各个tuple。
    //
    // 返回false表示拉起失败（fork失败或perRankArgs.size() != count），此时已拉起的
    // 子进程已被回收，直接返回错误即可。
    template <typename Entry, typename... Args>
    bool Launch(uint32_t count, Entry&& entry, std::vector<std::tuple<Args...>>&& perRankArgs)
    {
        if (perRankArgs.size() != static_cast<size_t>(count)) {
            return false;
        }
        if (activeGroup_ != nullptr) {
            return false;
        }
        InstallSignalHandlers();

        pids_.assign(count, -1);
        exitCodes_.assign(count, 0);
        for (uint32_t rank = 0; rank < count; ++rank) {
            pid_t pid = fork();
            if (pid < 0) {
                std::fprintf(stderr, "[ProcessGroup] fork rank %u failed: %s\n", rank, std::strerror(errno));
                // 回收已拉起的子进程，不留孤儿：0秒宽限直接升级到SIGKILL。
                ReapAll(std::chrono::seconds(0));
                return false;
            }
            if (pid == 0) {
                // 子进程：恢复默认信号处理，执行调用方入口后直接退出。
                // _exit不走父进程栈上对象的析构——ACL/HCCL句柄不能被二次析构。
                RestoreSignalHandlers();
                int code = 0;
                try {
                    code = std::apply(std::forward<Entry>(entry), std::move(perRankArgs[rank]));
                } catch (const std::exception& err) {
                    std::fprintf(stderr, "[ProcessGroup] rank %u threw: %s\n", rank, err.what());
                    code = 0xFF;
                } catch (...) {
                    code = 0xFF;
                }
                std::fflush(nullptr);
                _exit(code);
            }
            pids_[rank] = pid;
        }
        activeGroup_ = this;
        return true;
    }

    // 等待全部子进程退出。任一子进程失败不会提前放弃回收其余子进程——半途退出
    // 会留下阻塞在通信域或host barrier上的孤儿进程。
    // 返回true当且仅当所有子进程都正常退出且退出码为0。
    bool WaitAll()
    {
        if (pids_.empty()) {
            return true;
        }
        const auto deadline = options_.timeoutSeconds == 0U ?
                                  std::chrono::steady_clock::time_point::max() :
                                  std::chrono::steady_clock::now() + std::chrono::seconds(options_.timeoutSeconds);
        const bool allPassed = WaitAndReap(deadline);
        return allPassed && !interrupted_;
    }

    // 终止全部子进程并回收。SIGINT/SIGTERM到达父进程时自动触发，也可手动调用；
    // 对已退出的进程无副作用，可重复调用。
    void TerminateAll()
    {
        if (pids_.empty()) {
            return;
        }
        SignalAll(SIGTERM);
        ReapAll(std::chrono::seconds(options_.graceSeconds));
    }

    // 各rank的最终状态：正常退出为退出码（0-255），被信号杀死为负值（如-9），
    // 超时被杀同样表现为负值。WaitAll/TerminateAll完成后有效，未启动的rank为0。
    const std::vector<int>& ExitCodes() const { return exitCodes_; }

    // Ctrl+C/SIGTERM是否介入过本次运行，用于把"被中断"和"测试失败"区分开。
    bool Interrupted() const { return interrupted_; }

private:
    static constexpr std::chrono::milliseconds kPollInterval{50};

    // 每个rank的等待策略，见CollectOne：
    enum class WaitMode {
        blocking,     // 无限期阻塞等一个子进程
        nonBlocking,  // 只收割已退出的
        withDeadline, // 阻塞但每kPollInterval醒来检查deadline/信号
    };

    // 等待并收割全部子进程。返回true当且仅当全部正常退出且退出码为0。
    // deadline为time_point::max()时表示不限时（WaitMode::blocking路径）。
    bool WaitAndReap(std::chrono::steady_clock::time_point deadline)
    {
        const bool hasDeadline = deadline != std::chrono::steady_clock::time_point::max();
        bool allPassed = true;
        // 每圈循环收割当前已退出的子进程；未到deadline且还有子进程未退就继续等。
        while (HasRunningChild()) {
            if (signalFlag_ != 0) {
                interrupted_ = true;
                TerminateAll();
                return false;
            }
            if (hasDeadline && std::chrono::steady_clock::now() >= deadline) {
                std::fprintf(
                    stderr, "[ProcessGroup] timeout after %u s, terminating all ranks\n", options_.timeoutSeconds);
                TerminateAll();
                return false;
            }
            const bool roundPassed = CollectOne(hasDeadline ? WaitMode::withDeadline : WaitMode::blocking);
            if (!roundPassed) {
                allPassed = false;
            }
        }
        // 终止路径（TerminateAll -> ReapAll）已写过exitCodes_，这里不再覆盖。
        return allPassed;
    }

    // 收割一批已退出的子进程。返回true当且仅当本轮收割到的子进程都正常退出且退出码为0；
    // 没有子进程退出时返回true（不改变结论）。
    bool CollectOne(WaitMode mode)
    {
        bool roundPassed = true;
        while (true) {
            int status = 0;
            const int options = mode == WaitMode::nonBlocking ? WNOHANG : 0;
            const pid_t pid = waitpid(-1, &status, options);
            if (pid < 0) {
                if (errno == EINTR) {
                    // 被信号打断：回到外层循环重新检查signalFlag_与deadline。
                    return true;
                }
                // ECHILD：没有可等待的子进程，正常收尾。
                return roundPassed;
            }
            if (pid == 0) {
                // WNOHANG下没有已退出的子进程。
                return roundPassed;
            }
            if (!RecordExit(pid, status)) {
                roundPassed = false;
            }
            if (mode == WaitMode::nonBlocking) {
                continue; // 宽限期轮询路径：继续收割其余已退出的。
            }
            if (mode == WaitMode::withDeadline) {
                continue; // 超时路径：waitpid不带WNOHANG最多阻塞一个子进程退出，继续。
            }
            // blocking路径同样继续，直到waitpid返回<=0。
        }
    }

    // 把一个已退出子进程的状态记入exitCodes_。返回false表示该子进程异常
    // （非正常退出或退出码非0）。
    bool RecordExit(pid_t pid, int status)
    {
        for (size_t i = 0; i < pids_.size(); ++i) {
            if (pids_[i] == pid) {
                pids_[i] = -1;
                if (WIFEXITED(status)) {
                    exitCodes_[i] = WEXITSTATUS(status);
                    return WEXITSTATUS(status) == 0;
                }
                exitCodes_[i] = WIFSIGNALED(status) ? -WTERMSIG(status) : -1;
                return false;
            }
        }
        return true; // 不是本组的子进程（不应发生），忽略。
    }

    bool HasRunningChild() const
    {
        for (pid_t pid : pids_) {
            if (pid > 0) {
                return true;
            }
        }
        return false;
    }

    void SignalAll(int sig)
    {
        for (pid_t pid : pids_) {
            if (pid > 0) {
                kill(pid, sig);
            }
        }
    }

    // 向所有子进程发SIGTERM，等待grace时间后仍存活的收到SIGKILL，然后收割全部。
    // grace为killImmediately（0秒）时直接升级到SIGKILL，用于Launch失败的清理。
    void ReapAll(std::chrono::seconds grace)
    {
        if (!HasRunningChild()) {
            return;
        }
        SignalAll(SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + grace;
        while (HasRunningChild() && std::chrono::steady_clock::now() < deadline) {
            CollectOne(WaitMode::nonBlocking);
            if (HasRunningChild()) {
                // 轮询间隔为5ms（kPollInterval的1/10），避免短宽限期下错过窗口。
                std::this_thread::sleep_for(kPollInterval / 10U);
            }
        }
        if (HasRunningChild()) {
            SignalAll(SIGKILL);
        }
        while (HasRunningChild()) {
            CollectOne(WaitMode::blocking);
        }
    }

    void InstallSignalHandlers()
    {
        struct sigaction action {};
        action.sa_handler = &ProcessGroup::OnSignal;
        sigemptyset(&action.sa_mask);
        // SA_RESTART使被信号中断的系统调用自动重启，避免waitpid等阻塞调用返回EINTR。
        action.sa_flags = SA_RESTART;
        sigaction(SIGINT, &action, &previousInt_);
        sigaction(SIGTERM, &action, &previousTerm_);
    }

    void RestoreSignalHandlers()
    {
        sigaction(SIGINT, &previousInt_, nullptr);
        sigaction(SIGTERM, &previousTerm_, nullptr);
    }

    // 信号处理器只做async-signal-safe的事：写标志 + 向子进程补发SIGTERM。
    // 交互式Ctrl+C时内核会把SIGINT发给整个前台进程组，子进程已各自收到；
    // 这里补发覆盖非交互场景（kill只打父进程）。
    static void OnSignal(int sig)
    {
        if (activeGroup_ != nullptr) {
            activeGroup_->signalFlag_ = sig;
            activeGroup_->SignalAll(sig);
        }
    }

    ProcessGroupOptions options_;
    std::vector<pid_t> pids_;
    std::vector<int> exitCodes_;
    volatile sig_atomic_t signalFlag_ = 0;
    bool interrupted_ = false;
    struct sigaction previousInt_ {};
    struct sigaction previousTerm_ {};

    static ProcessGroup* activeGroup_;
};

// 静态成员定义。header-only下放于命名空间作用域，每个包含本头文件的翻译单元
// 各持有一份（样例均为单可执行文件单翻译单元使用，不构成问题）。
inline ProcessGroup* ProcessGroup::activeGroup_ = nullptr;

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_PROCESS_MANAGER_H
