#include <exception>
#include <iostream>
#include <string_view>

void run_box_downsampler_tests();
void run_sampled_decode_tests();

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
  const bool downsamplerPassed =
      run_suite("box downsampler", run_box_downsampler_tests);
  const bool sampledPassed = run_suite("sampled decode", run_sampled_decode_tests);
  return downsamplerPassed && sampledPassed ? 0 : 1;
}
