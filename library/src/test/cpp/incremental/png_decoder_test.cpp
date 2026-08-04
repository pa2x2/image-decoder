#include "fixtures/png_fixture.h"
#include "incremental/png_decoder.h"
#include "support/decode_harness.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

const IncrementalDecoderFactory kPngFactory = create_incremental_png_decoder;

void require_useful_precompletion_update(const IncrementalDecodeTrace& trace) {
  const auto update =
      std::find_if(trace.stillUpdates.begin(), trace.stillUpdates.end(),
                   [](const CapturedStillUpdate& candidate) {
                     return !candidate.receivedAtEndOfInput &&
                            populated_pixel_count(candidate) > 0;
                   });
  require_condition(update != trace.stillUpdates.end(),
                    "PNG must publish useful pixels before end of input");
}

void require_truncated_png_fails(const EncodedImageFixture& fixture) {
  const size_t truncatedSize = fixture.encoded.size() * 3 / 4;
  std::vector<uint8_t> truncated(fixture.encoded.begin(),
                                 fixture.encoded.begin() + truncatedSize);
  bool threw = false;
  try {
    decode_in_chunks(kPngFactory,
                     full_size_options(fixture.width, fixture.height),
                     truncated, 521);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require_condition(threw, "truncated PNG must fail at end of input");
}

void run_png_case(bool adam7) {
  const auto fixture = make_png_fixture(adam7);
  const auto trace = decode_in_chunks(
      kPngFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 521);
  require_valid_completed_trace(trace, fixture.width, fixture.height);
  require_useful_precompletion_update(trace);
  if (adam7) {
    require_condition(trace.stillUpdates.size() >= 2,
                      "Adam7 PNG must publish multiple generations");
  }
  require_condition(maximum_channel_error(trace.stillUpdates.back().rgba,
                                          fixture.expectedRgba) <= 1,
                    "PNG final pixels must match the source, including alpha");
  require_truncated_png_fails(fixture);
}

void require_apng_falls_back() {
  const auto encoded = make_apng_fallback_fixture();
  const auto trace =
      decode_in_chunks(kPngFactory, full_size_options(192, 384), encoded, 17);
  require_condition(trace.result == IncrementalBackendResult::Unsupported,
                    "APNG must fall back to the animation pipeline");
  require_condition(!trace.metadata.has_value() && trace.stillUpdates.empty(),
                    "APNG fallback must not advertise static output");
}

} // namespace

void run_png_decoder_tests() {
  run_png_case(false);
  run_png_case(true);
  require_apng_falls_back();
}
