// test_capi.cpp — verify the C ABI against the recorded CLI baseline.
// Every row is (format, expected steps, expected reason) for start=1.0,
// multiplier=1.02, timeout=0, X102 multiply walk. Returns non-zero on mismatch.
#include "c_api.h"
#include <math.h>
#include <stdio.h>

struct Row {
    int id;
    const char* name;
    long long steps;
    int reason;
};

int main(void) {
    static const Row rows[] = {
        {EAFRDT_FP4,       "fp4",        16,      EAFRDT_IDENTITY},
        {EAFRDT_FP6,       "fp6",        16,      EAFRDT_IDENTITY},
        {EAFRDT_FP8_E4M3,  "fp8-e4m3",   16,      EAFRDT_IDENTITY},
        {EAFRDT_FP8_E5M2,  "fp8-e5m2",   16,      EAFRDT_IDENTITY},
        {EAFRDT_FP16,      "f16",        574,     EAFRDT_OVERFLOW},
        {EAFRDT_BF16,      "bf16",       3913,    EAFRDT_OVERFLOW},
        {EAFRDT_TF32,      "tf32",       4623,    EAFRDT_OVERFLOW},
        {EAFRDT_F32,       "f32",        4481,    EAFRDT_OVERFLOW},
        {EAFRDT_F64,       "f64",        35843,   EAFRDT_OVERFLOW},
        {EAFRDT_X87,       "x87",        573521,  EAFRDT_OVERFLOW},
        {EAFRDT_Q128,      "fp128",      573521,  EAFRDT_OVERFLOW},
        {EAFRDT_BID32,     "dec32[BID]", 11279,   EAFRDT_OVERFLOW},
        {EAFRDT_BID64,     "dec64[BID]", 44767,   EAFRDT_OVERFLOW},
        {EAFRDT_BID128,    "dec128[BID]",715048,  EAFRDT_OVERFLOW},
        {EAFRDT_DPD32,     "dpd32",      11279,   EAFRDT_OVERFLOW},
        {EAFRDT_DPD64,     "dpd64",      44767,   EAFRDT_OVERFLOW},
        {EAFRDT_DPD128,    "dpd128",     714521,  EAFRDT_OVERFLOW},
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    int fails = 0;

    printf("%s\n", eafrdt_version());
    for (int i = 0; i < n; ++i) {
        int reason = -1;
        long long steps = eafrdt_walk((enum eafrdt_fmt)rows[i].id, 1.0, 1.02, 0.0, &reason);
        int ok = (steps == rows[i].steps && reason == rows[i].reason);
        printf("  %-12s  %8lld steps  reason=%d  %s\n",
               rows[i].name, steps, reason, ok ? "OK" : "MISMATCH");
        if (!ok) {
            printf("               expected %lld steps reason=%d\n", rows[i].steps, rows[i].reason);
            ++fails;
        }
    }

    // Regression: a multiplier that rounds back to the same value must stop via
    // identity (16 steps) instead of running forever with timeout=0.
    {
        int reason = -1;
        long long steps = eafrdt_walk(EAFRDT_FP4, 1.0, 1.02, 0.0, &reason);
        int ok = (steps == 16 && reason == EAFRDT_IDENTITY);
        printf("  identity-guard fp4 timeout=0  %lld steps reason=%d  %s\n",
               steps, reason, ok ? "OK" : "MISMATCH");
        if (!ok) ++fails;
    }

    // Underflow: 0.1 factor must terminate at zero with reason=1, not spin.
    {
        int reason = -1;
        long long steps = eafrdt_walk(EAFRDT_F32, 1.0, 0.1, 0.0, &reason);
        int ok = (reason == EAFRDT_UNDERFLOW && steps > 0);
        printf("  underflow f32*0.1  %lld steps reason=%d  %s\n",
               steps, reason, ok ? "OK" : "MISMATCH");
        if (!ok) ++fails;
    }

    // NaN: 0 * inf must exit immediately instead of spinning on a non-finite value.
    {
        int reason = -1;
        long long steps = eafrdt_walk(EAFRDT_F32, 0.0, (double)INFINITY, 0.0, &reason);
        int ok = (reason == EAFRDT_NOT_A_NUMBER && steps > 0);
        printf("  nan-guard f32 0*inf  %lld steps reason=%d  %s\n",
               steps, reason, ok ? "OK" : "MISMATCH");
        if (!ok) ++fails;
    }

    printf("%s (%d checks, %d failures)\n", fails ? "FAILED" : "ALL TESTS PASSED", n + 3, fails);
    return fails ? 1 : 0;
}
