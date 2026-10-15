# Building DriveByPreAim.asi

Source: `dllmain.cpp` (GPL-3.0). Depends on zolika1351's iv-sdk.

## 1. Get iv-sdk (the exact commit used)

    git clone https://github.com/zolika1351/iv-sdk
    cd iv-sdk
    git checkout dddbd3f54b146bdb3a9852809e48e5cb169af3cd
    git apply ../iv-sdk-mingw-compat.patch   # only needed for the MinGW/Zig build below

`iv-sdk-mingw-compat.patch` makes two small changes so iv-sdk compiles with Clang/MinGW:
MSVC `__try/_asm` in `CTheScripts::FindNativeAddress` is replaced with GCC-style inline asm,
and a `reinterpret_cast` from `nullptr` in `NewAddressSet.h` is replaced with a C-style cast.

Folder layout expected by the `#include`:

    some-folder/
      iv-sdk/
      DriveByPreAim/dllmain.cpp

## 2. Compile (32-bit Windows DLL)

Built with Zig 0.13.0 (pip install ziglang==0.13.0), from the DriveByPreAim folder:

    python -m ziglang c++ -target x86-windows-gnu -std=c++17 -shared -O2 -fms-extensions \
      -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE -static -s \
      -o DriveByPreAim.asi dllmain.cpp -lshlwapi -lversion -luser32 -lkernel32

It should also build with MSVC (x86, C++17) against unpatched iv-sdk, as iv-sdk's own
examples do, though that configuration hasn't been tested.
