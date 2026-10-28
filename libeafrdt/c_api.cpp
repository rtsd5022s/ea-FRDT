// c_api.cpp — C ABI implementation
#include "c_api.h"
#include "eafrdt.h"

using namespace fw::core;

long long eafrdt_walk(enum eafrdt_fmt fmt, double start, double mult,
                      double timeout_sec, int* reason) {
    int r = 0;
    long long n = 0;
    switch (fmt) {
    case EAFRDT_FP4:    n = walk_binary(fmt_fp4(),    start, mult, timeout_sec, &r); break;
    case EAFRDT_FP6:    n = walk_binary(fmt_fp6(),    start, mult, timeout_sec, &r); break;
    case EAFRDT_FP8_E4M3:n= walk_binary(fmt_fp8e4m3(),start, mult, timeout_sec, &r); break;
    case EAFRDT_FP8_E5M2:n=walk_binary(fmt_fp8e5m2(),start, mult, timeout_sec, &r); break;
    case EAFRDT_FP16:   n = walk_binary(fmt_fp16(),  start, mult, timeout_sec, &r); break;
    case EAFRDT_BF16:   n = walk_binary(fmt_bf16(),  start, mult, timeout_sec, &r); break;
    case EAFRDT_TF32:   n = walk_binary(fmt_tf32(),   start, mult, timeout_sec, &r); break;
    case EAFRDT_F32:    n = walk_f32((float)start, (float)mult, timeout_sec, &r); break;
    case EAFRDT_F64:    n = walk_f64(start, mult, timeout_sec, &r); break;
    case EAFRDT_X87:    n = walk_x87(start, mult, timeout_sec, &r); break;
    case EAFRDT_Q128:   n = walk_q128(start, mult, timeout_sec, &r); break;
    case EAFRDT_BID32:  n = walk_bid(bid_fmt32(), start, mult, timeout_sec, &r); break;
    case EAFRDT_BID64:  n = walk_bid(bid_fmt64(), start, mult, timeout_sec, &r); break;
    case EAFRDT_BID128: n = walk_bid128(start, mult, timeout_sec, &r); break;
    case EAFRDT_DPD32:  n = walk_dpd(dpd_fmt32(), start, mult, timeout_sec, &r); break;
    case EAFRDT_DPD64:  n = walk_dpd(dpd_fmt64(), start, mult, timeout_sec, &r); break;
    case EAFRDT_DPD128: n = walk_dpd(dpd_fmt128(),start, mult, timeout_sec, &r); break;
    }
    if (reason) *reason = r;
    return n;
}

const char* eafrdt_version(void) {
    return "ea-FRDT core v1.0 (libeafrdt, clang64, C++23)";
}
