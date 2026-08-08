#include <exception>
#include <iostream>
#include <string>
#include <string_view>

void run_jpeg_decoder_tests();
void run_gif_decoder_tests();
void run_jxl_decoder_tests();
void run_jxl_decoder_benchmark();
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

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--benchmark-jxl") {
    try {
      run_jxl_decoder_benchmark();
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "FAIL JXL benchmark: " << error.what() << '\n';
      return 1;
    }
  }
  const bool jpegPassed = run_suite("incremental JPEG", run_jpeg_decoder_tests);
  const bool gifPassed = run_suite("incremental GIF", run_gif_decoder_tests);
  const bool jxlPassed = run_suite("incremental JXL", run_jxl_decoder_tests);
  const bool pngPassed = run_suite("incremental PNG", run_png_decoder_tests);
  const bool webpPassed = run_suite("incremental WebP", run_webp_decoder_tests);
  const bool sessionPassed =
      run_suite("incremental session", run_session_tests);
  return jpegPassed && gifPassed && jxlPassed && pngPassed && webpPassed && sessionPassed
             ? 0
             : 1;
}
