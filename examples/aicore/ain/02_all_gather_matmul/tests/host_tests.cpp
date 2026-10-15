/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 * See LICENSE in the root of the software repository.
 */
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "options.h"
#include "reference.h"

using namespace ain_agmm;
void Expect(bool pass) { if (!pass) { throw std::runtime_error("host check failed"); } }
template <typename F> void Reject(F f)
{
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    Expect(rejected);
}

Options Parse(std::vector<std::string> args)
{
    std::vector<char*> argv;
    for (auto& arg : args) { argv.push_back(arg.data()); }
    return ParseOptions(static_cast<int>(argv.size()), argv.data());
}

int main()
{
    Reject([] { MakeTiling(0, 16, 16, 2, 1, 0); });
    Reject([] { MakeTiling(128, 17, 16, 2, 1, 0); });
    Reject([] { MakeTiling(128, 16, 17, 2, 1, 0); });
    Reject([] { MakeTiling(512, 256, 256, 2, 8, 257); });
    Reject([] { MakeTiling(UINT32_MAX, 0xfffffff0U, 0xfffffff0U, 64, 48, 0); });
    Reject([] { MakeTiling(512, 16, 1048592, 2, 1, 256); });
    Reject([] { CheckedProduct(UINT64_MAX, 2); });
    Reject([] { Parse({"demo", "tcp://127.0.0.1:1", "2", "64", "16", "16", "--devices", "1,1"}); });
    Reject([] { Parse({"demo", "tcp://127.0.0.1:1", "2", "64", "16", "16", "--iters", "0"}); });
    Reject([] { Parse({"demo", "tcp://127.0.0.1:1", "2", "64", "16", "16", "--dtype", "fp32"}); });
    const auto o = Parse({"demo", "tcp://127.0.0.1:1", "2", "64", "16", "16", "--devices", "1,3", "--random"});
    Expect(o.devices == std::vector<uint32_t>({1, 3}) && o.random);

    // Communication address coverage: odd tails, more than 16 rounds, and >4 GiB
    // offsets. Every row must occur once and each operation fits its window.
    for (uint32_t m : {1U, 17U, 255U, 256U, 257U, 4097U, 65537U}) {
        for (uint32_t ranks : {2U, 4U, 8U}) {
            const auto t = MakeTiling(m, 272, 16384, ranks, 8, m > 256 ? 256 : m);
            uint64_t next = 0;
            for (uint32_t j = 0; j < t.rounds; ++j) {
                const uint64_t row = static_cast<uint64_t>(j) * t.tileM;
                const uint64_t rows = std::min<uint64_t>(t.tileM, t.m - row);
                Expect(row == next && rows > 0 && rows * t.k * 2 <= MAX_GET_BYTES);
                next += rows;
                for (uint32_t p = 0; p < ranks; ++p) {
                    const uint64_t end = (static_cast<uint64_t>(p) * m + row + rows) * t.k * 2;
                    Expect(end <= static_cast<uint64_t>(ranks) * m * t.k * 2);
                }
            }
            Expect(next == m);
        }
    }
    for (bool bf16 : {false, true}) {
        for (int i = -128; i <= 128; ++i) {
            const float v = i / 16.0F;
            Expect(Decode(Encode(v, bf16), bf16) == v);
        }
        Expect(std::isnan(Decode(Encode(NAN, bf16), bf16)));
        Expect(std::isinf(Decode(Encode(INFINITY, bf16), bf16)));
    }
    Expect(Encode(1.0F, false) == 0x3c00 && Encode(1.0F, true) == 0x3f80);
    Expect(Encode(std::ldexp(1.0F, -24), false) == 1);
    Expect(Encode(1.00048828125F, false) == 0x3c00); // Tie to even.
    // Independent dense reference must agree with the fast periodic golden.
    for (uint32_t seed : {1U, 2U}) {
        for (uint32_t p = 0; p < 4; ++p) {
            for (uint32_t m = 0; m < 17; ++m) {
                for (uint32_t n = 0; n < 19; ++n) {
                    float sum = 0;
                    for (uint32_t k = 0; k < 80; ++k) {
                        sum += InputValue(p, m, k % 16, seed, false) * InputValue(3, k % 16, n, seed, true);
                    }
                    Expect(sum == Golden(p, 3, m, n, 80, seed, false));
                }
            }
        }
    }
    std::cout << "PASS: host tiling, argument validation and reference checks (not device execution)\n";
}
