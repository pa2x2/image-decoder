#include <exception>
#include <iostream>
#include <string_view>

void run_box_block_sums_tests();
void run_box_downsampler_tests();
void run_sampled_decode_memory_tests();
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
  const bool blockSumsPassed =
      run_suite("box block sums", run_box_block_sums_tests);
  const bool downsamplerPassed =
      run_suite("box downsampler", run_box_downsampler_tests);
  const bool sampledPassed = run_suite("sampled decode", run_sampled_decode_tests);
  const bool sampledMemoryPassed =
      run_suite("sampled decode memory", run_sampled_decode_memory_tests);
  return blockSumsPassed && downsamplerPassed && sampledPassed &&
                 sampledMemoryPassed
             ? 0
             : 1;
}
