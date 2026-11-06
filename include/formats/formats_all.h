// formats_all.h — Aggregator: includes every floating-point format header.
// Each format header binds its own parameters to a shared engine (engine_*.h) or,
// for x87/fp128/dec128, contains its self-contained algorithm.
#pragma once
#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif

// Binary hand-rolled bit-level (engine_sim.h)
#include "fp4.h"
#include "fp6.h"
#include "fp8_e4m3.h"
#include "fp8_e5m2.h"
#include "bf16.h"
#include "tf32.h"

// Native IEEE types (engine_native.h)
#include "fp16.h"
#include "fp32.h"
#include "fp64.h"

// BID decimal (engine_bid_codec.h + engine_decnum.h; dec128.h holds its own BigDec engine)
#include "dec32.h"
#include "dec64.h"
#include "dec128.h"

// DPD decimal (engine_dpd_codec.h + engine_dpd.h)
#include "dpd32.h"
#include "dpd64.h"
#include "dpd128.h"

// Self-contained extended precision
#include "x87.h"
#include "fp128.h"
