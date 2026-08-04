#include <exception>
#include <iostream>
#include <string_view>

void run_jpeg_decoder_tests();
void run_png_decoder_tests();
void run_session_tests();
void run_webp_decoder_tests();

namespace {

bool run_suite(std::string_view name, void (*suite)()) {
  try {
    suite();
    std::cout << "PASS " << name << '\n';
    return true;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    return false;
  }
}

} // namespace

int main() {
  const bool jpegPassed = run_suite("incremental JPEG", run_jpeg_decoder_tests);
  const bool pngPassed = run_suite("incremental PNG", run_png_decoder_tests);
  const bool webpPassed = run_suite("incremental WebP", run_webp_decoder_tests);
  const bool sessionPassed =
      run_suite("incremental session", run_session_tests);
  return jpegPassed && pngPassed && webpPassed && sessionPassed ? 0 : 1;
}
