# Host-native decoder tests

This harness compiles the production decoders, the incremental session and the
downsamplers directly. It generates encoded fixtures in memory and checks
pre-completion still updates, final pixels, malformed/truncated input,
animation fallback, event ordering, pending-update coalescing, sampled decodes
and alpha handling without an Android device.

The decoders link the same versions of libjpeg-turbo, libpng, libwebp, libjxl
and lcms2 that the shipped library pins: `CMakeLists.txt` includes
`../main/pinned_libraries.cmake`, which fetches and builds them. The host needs
CMake, Ninja, nasm, a C and C++20 compiler, a JDK for the JNI headers and
network access for the first configure. The first build compiles every
dependency and takes several minutes.

From the repository root:

```shell
cmake -S library/native/test -B build/host-native-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-native-tests
ctest --test-dir build/host-native-tests --output-on-failure
```

For an AddressSanitizer and UndefinedBehaviorSanitizer run, which also
instruments the dependencies:

```shell
cmake -S library/native/test -B build/host-native-tests-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/host-native-tests-sanitized
ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir build/host-native-tests-sanitized --output-on-failure
```
