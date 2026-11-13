# JNI bindings

`katek jni` generates C wrappers for `pub fn` functions in `src/lib.akt`
or `src/main.akt`.

```bash
katek jni --package com.example --class Main
# target/jni_bindings.c + target/libapp.dll (Windows) or .so
```

Without `--no-build`, the command also compiles the JNI shared library
(native JDK headers, or NDK headers when NDK is installed).
Verified on Windows: Java `com.example.Main.add(2, 3)` returns `5`.

Supported types: `int/i32`, `i64`, `f32`, `f64`, `bool`, `string`,
`*void` (`jobject`), `[]i32` (`jintArray`), `[]f64` (`jdoubleArray`),
`void`.
Strings are converted with `GetStringUTFChars`/`NewStringUTF`.

Build the `.so` with NDK:

```bash
katek build --target android-arm64 --release
```

Objects, arrays, `jobject` parameters and JNI overloads are not generated
yet.
