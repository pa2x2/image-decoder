# Host-native incremental decoder tests

This harness compiles the production incremental session and JPEG, PNG, and
WebP backends directly. It generates encoded fixtures in memory and checks
pre-completion still updates, final pixels, malformed/truncated input,
animation fallback, event ordering, and pending-update coalescing without an
Android device.

The host needs CMake, a C++20 compiler, and development packages discoverable
through CMake or `pkg-config` for libjpeg, libpng, libwebp, and lcms2. These are
host test dependencies; Android release builds continue to use the versions
pinned by the library's production CMake configuration.

From the repository root:

```shell
cmake -S library/src/test/cpp -B build/host-native-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-native-tests
ctest --test-dir build/host-native-tests --output-on-failure
```

For an AddressSanitizer and UndefinedBehaviorSanitizer run:

```shell
cmake -S library/src/test/cpp -B build/host-native-tests-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/host-native-tests-sanitized
ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir build/host-native-tests-sanitized --output-on-failure
```
