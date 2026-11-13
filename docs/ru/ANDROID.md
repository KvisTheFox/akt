# Android NDK

akt умеет собирать `.so` для Android через NDK Clang.

## 1. Установка NDK

Установите Android Studio → SDK Manager → NDK, либо скачайте NDK
отдельно. `katek` ищет NDK в:

- `ANDROID_NDK_HOME`
- `ANDROID_NDK_ROOT`
- `%LOCALAPPDATA%\Android\Sdk\ndk\*`
- `C:\Android\ndk`

Проверка:

```powershell
katek doctor
```

## 2. Библиотека

```bash
katek new mylib --lib
cd mylib
katek build --target android-arm64 --release
```

Результат:

```text
target/libapp.so
```

ABI:

| Команда | Android ABI | Triple |
|---|---|---|
| `--target android-arm64` | `arm64-v8a` | `aarch64-linux-android21` |
| `--target android-arm` | `armeabi-v7a` | `armv7a-linux-androideabi21` |
| `--target android-x86_64` | `x86_64` | `x86_64-linux-android21` |

## 3. Ручная сборка

```bash
aktc build src/lib.akt --shared --release \
  --target aarch64-linux-android21 \
  --sysroot "$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot" \
  --cc "$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe" \
  -o target/libapp.so
```

## 4. Подключение к APK

Скопируйте `.so` в:

```text
app/src/main/jniLibs/arm64-v8a/libapp.so
```

Для JNI нужен C-мост с именем `Java_<package>_<Class>_<method>`:
сгенерируйте C через `aktc build --keep-c`, добавьте JNI-обёртку и
соберите `.so` тем же NDK Clang. Прямой экспорт JNI-имён из akt —
следующий этап.

## 5. Ограничения

- `main` в shared-библиотеке не используется.
- Android API level по умолчанию 21.
- Нет автоматического JNI-bindgen.
- Нет упаковки APK/AAB — только `.so`.
- Для `armeabi-v7a` используйте `--target android-arm`.
