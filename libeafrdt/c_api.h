// c_api.h — C ABI for libeafrdt. Link against libeafrdt.a.
#ifdef __cplusplus
extern "C" {
#endif

// Format IDs
enum eafrdt_fmt {
    EAFRDT_FP4 = 0,
    EAFRDT_FP6,
    EAFRDT_FP8_E4M3,
    EAFRDT_FP8_E5M2,
    EAFRDT_FP16,
    EAFRDT_BF16,
    EAFRDT_TF32,
    EAFRDT_F32,
    EAFRDT_F64,
    EAFRDT_X87,
    EAFRDT_Q128,
    EAFRDT_BID32,
    EAFRDT_BID64,
    EAFRDT_BID128,
    EAFRDT_DPD32,
    EAFRDT_DPD64,
    EAFRDT_DPD128
};

// Exit reasons
enum eafrdt_reason {
    EAFRDT_OVERFLOW = 0,
    EAFRDT_UNDERFLOW = 1,
    EAFRDT_TIMEOUT = 2
};

// Walk one format from `start` multiplying by `multiplier` until exit.
// Returns step count. Sets *reason to exit reason (may be NULL).
// timeout_sec=0 means unlimited.
long long eafrdt_walk(enum eafrdt_fmt fmt,
                      double start,
                      double multiplier,
                      double timeout_sec,
                      int* reason);

// Library version string.
const char* eafrdt_version(void);

#ifdef __cplusplus
}
#endif
