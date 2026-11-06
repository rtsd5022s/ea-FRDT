---
name: Build issue
about: Build failure, compiler error, or link error on a supported platform
title: "[BUILD] "
labels: build
---

**Environment**
- OS: (Windows / Linux)
- Compiler: (clang++ version / g++ version)
- Architecture: (x86_64 / arm64 / other)
- On Windows: are you using MSYS2 **clang64**? (MSVC and LLVM's default windows-msvc clang are rejected by design.)

**Build command**
```
(paste the exact cmake or raw compiler command)
```

**Full error output**
```
(paste the complete compiler output, including all errors and warnings)
```

**Expected**
What you expected to build.

**Before submitting**
- MSVC cl.exe is not supported. Use MSYS2 clang64 on Windows.
- LLVM's official prebuilt clang targeting `x86_64-pc-windows-msvc` is also rejected by the `_WIN32 && !__MINGW32__` header guard. Use MSYS2.
- g++ 16.2.0 has a known internal compiler error on this project; fall back to Clang.
- On Windows, link `winmm`.
- On Linux, no extra libs needed beyond pthread.
