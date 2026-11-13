# JNI bindings

`katek jni` генерирует C-обёртки для `pub fn` из `src/lib.akt` или
`src/main.akt`.

```bash
katek jni --package com.example --class Main
```

Без `--no-build` команда:
1. генерирует `target/jni_bindings.c`;
2. собирает `target/libapp.dll` (Windows native) или `target/libapp.so`;
3. при наличии NDK собирает Android `.so`.

Проверено на Windows: Java-класс `com.example.Main` вызывает
сгенерированный `add(2, 3)` и получает `5`.

Пример сгенерированного кода:

```c
JNIEXPORT jint JNICALL Java_com_example_Main_add(
    JNIEnv* env, jobject thiz, jint a, jint b) {
    int _r = add((int)a, (int)b);
    return (jint)_r;
}
```

Поддерживаемые типы:

| akt | JNI |
|---|---|
| `int`, `i32` | `jint` |
| `i64` | `jlong` |
| `f32` | `jfloat` |
| `f64` | `jdouble` |
| `bool` | `jboolean` |
| `string` | `jstring` |
| `*void` | `jobject` |
| `[]i32` | `jintArray` |
| `[]f64` | `jdoubleArray` |
| `void` | `void` |

Строки конвертируются через `GetStringUTFChars` /
`ReleaseStringUTFChars` и `NewStringUTF`.

## Сборка .so

Нужен NDK:

```bash
katek build --target android-arm64 --release
# target/libapp.so
```

Для ручной сборки с JNI:

```bash
aktc build src/lib.akt --keep-c --shared --release \
  --target aarch64-linux-android21 \
  --sysroot "$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot" \
  --cc "$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe" \
  -o target/libapp.so

"$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe" \
  -shared -fPIC -O2 \
  -I runtime/include \
  -I "$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include" \
  src/lib.akt.gen.c target/jni_bindings.c runtime/src/akt_runtime.c \
  -o target/libapp.so
```

Скопируйте `libapp.so` в `app/src/main/jniLibs/arm64-v8a/`.

Ограничения: объекты, массивы, `jobject`-параметры и перегрузки JNI пока
не генерируются.
