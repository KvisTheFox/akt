# Android NDK

akt can build Android `.so` libraries with NDK Clang.

## Setup

Install NDK via Android Studio or standalone. `katek` looks in:

- `ANDROID_NDK_HOME`
- `ANDROID_NDK_ROOT`
- `%LOCALAPPDATA%\Android\Sdk\ndk\*`
- `C:\Android\ndk`

```powershell
katek doctor
```

## Build a library

```bash
katek new mylib --lib
cd mylib
katek build --target android-arm64 --release
```

Output: `target/libapp.so`.

| Target | ABI | Triple |
|---|---|---|
| `android-arm64` | `arm64-v8a` | `aarch64-linux-android21` |
| `android-arm` | `armeabi-v7a` | `armv7a-linux-androideabi21` |
| `android-x86_64` | `x86_64` | `x86_64-linux-android21` |

Manual:

```bash
aktc build src/lib.akt --shared --release \
  --target aarch64-linux-android21 \
  --sysroot "$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot" \
  --cc "$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe" \
  -o target/libapp.so
```

Copy to `app/src/main/jniLibs/arm64-v8a/libapp.so`.

JNI exports (`Java_...`) require a C bridge for now; JNI bindgen is
planned.
