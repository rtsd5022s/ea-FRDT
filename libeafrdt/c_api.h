#if defined(_WIN32) && !defined(__MINGW32__)
#error "ea-FRDT requires MinGW target (x86_64-w64-mingw32 / x86_64-w64-windows-gnu). Do not use MSVC or LLVM's default windows-msvc clang; use MSYS2 clang64."
#endif
// c_api.h — C ABI for libeafrdt. Link against libeafrdt.dll (or the static archive).
//
// Windows DLL consumers must define EAFRDT_USE_DLL before including this header;
// the DLL itself is built with EAFRDT_BUILD_DLL. Static-archive consumers need
// neither macro.
#if defined(_WIN32)
#  if defined(EAFRDT_BUILD_DLL)
#    define EAFRDT_API __declspec(dllexport)
#  elif defined(EAFRDT_USE_DLL)
#    define EAFRDT_API __declspec(dllimport)
#  else
#    define EAFRDT_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define EAFRDT_API __attribute__((visibility("default")))
#else
#  define EAFRDT_API
#endif

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
    EAFRDT_TIMEOUT = 2,
    EAFRDT_IDENTITY = 3,      // 16 consecutive steps left the value unchanged
    EAFRDT_NOT_A_NUMBER = 4   // NaN reached (native float formats only)
};

// Walk one format from `start` multiplying by `multiplier` until exit.
// Returns step count. Sets *reason to exit reason (may be NULL).
// timeout_sec=0 means unlimited.
EAFRDT_API long long eafrdt_walk(enum eafrdt_fmt fmt,
                      double start,
                      double multiplier,
                      double timeout_sec,
                      int* reason);

// Library version string.
EAFRDT_API const char* eafrdt_version(void);

#ifdef __cplusplus
}
#endif
