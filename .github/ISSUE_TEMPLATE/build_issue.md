---
name: Build issue
about: Build failure, compiler error, or link error
title: "[BUILD] "
labels: build
---

**Environment**
- OS: (Windows / Linux / macOS)
- Compiler: (g++ version, clang++ version, MSVC version)
- Architecture: (x86_64 / ARM64 / other)

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

**Notes**
- MSVC is not supported. Use GCC 13+ or Clang 16+.
- g++ 16.2.0 has a known internal compiler error; use Clang if you hit it.
- On Windows, link winmm.
