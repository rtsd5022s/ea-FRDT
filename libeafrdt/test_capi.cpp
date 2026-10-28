// test_capi.c — verify C ABI
#include "c_api.h"
#include <stdio.h>

int main(void) {
    int reason;
    struct { int id; const char* name; } fmts[] = {
        {EAFRDT_F32, "f32"}, {EAFRDT_F64, "f64"},
        {EAFRDT_X87, "x87"}, {EAFRDT_Q128, "fp128"},
        {EAFRDT_BID32, "dec32[BID]"}, {EAFRDT_BID64, "dec64[BID]"},
        {EAFRDT_BID128, "dec128[BID]"},
        {EAFRDT_DPD32, "dpd32"}, {EAFRDT_DPD64, "dpd64"},
        {EAFRDT_DPD128, "dpd128"},
    };
    printf("%s\n", eafrdt_version());
    for (int i = 0; i < (int)(sizeof(fmts)/sizeof(fmts[0])); ++i) {
        long long n = eafrdt_walk((enum eafrdt_fmt)fmts[i].id, 1.0, 1.02, 30.0, &reason);
        printf("  %-14s  %lld steps  reason=%d\n", fmts[i].name, n, reason);
    }
    return 0;
}
