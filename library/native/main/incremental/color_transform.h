#ifndef IMAGEDECODER_INCREMENTAL_COLOR_TRANSFORM_H
#define IMAGEDECODER_INCREMENTAL_COLOR_TRANSFORM_H

#include "incremental/format_decoder.h"

#include <lcms2.h>

#include <cstdint>

class IncrementalColorTransform {
public:
  explicit IncrementalColorTransform(
      const IncrementalDecodeOptionsNative& options);
  ~IncrementalColorTransform();

  IncrementalColorTransform(const IncrementalColorTransform&) = delete;
  IncrementalColorTransform&
  operator=(const IncrementalColorTransform&) = delete;

  void configure(cmsHPROFILE sourceProfile, cmsUInt32Number inputType,
                 bool copyAlpha, bool forceOpaque);
  void apply(const uint8_t* input, uint8_t* output, uint32_t pixelCount) const;

private:
  cmsHPROFILE targetProfile = nullptr;
  cmsHTRANSFORM transform = nullptr;
  bool forceOpaqueOutput = false;
};

#endif // IMAGEDECODER_INCREMENTAL_COLOR_TRANSFORM_H
