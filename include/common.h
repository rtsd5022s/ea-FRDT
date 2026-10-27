// common.h — 聚合入口：按需 include 各模块
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef __clang__
#include <stdfloat>
#endif

#if defined(__clang__)
using NativeF16 = _Float16;
using NativeF32 = float;
using NativeF64 = double;
#else
using NativeF16 = std::float16_t;
using NativeF32 = std::float32_t;
using NativeF64 = std::float64_t;
#endif

#include "base_types.h"
#include "lang.h"
#include "color.h"
#include "format_util.h"
#include "drive.h"
