#include "incremental/color_transform.h"

#include <cstring>
#include <stdexcept>

IncrementalColorTransform::IncrementalColorTransform(
    const IncrementalDecodeOptionsNative& options) {
  if (!options.displayProfile.empty()) {
    targetProfile = cmsOpenProfileFromMem(options.displayProfile.data(),
                                          options.displayProfile.size());
  }
  if (targetProfile == nullptr) {
    targetProfile = cmsCreate_sRGBProfile();
  }
  if (targetProfile == nullptr) {
    throw std::runtime_error("Failed to create incremental target profile");
  }
}

IncrementalColorTransform::~IncrementalColorTransform() {
  if (transform != nullptr) {
    cmsDeleteTransform(transform);
  }
  if (targetProfile != nullptr) {
    cmsCloseProfile(targetProfile);
  }
}

void IncrementalColorTransform::configure(cmsHPROFILE sourceProfile,
                                          cmsUInt32Number inputType,
                                          bool copyAlpha, bool forceOpaque) {
  if (sourceProfile == nullptr) {
    throw std::runtime_error("Incremental source profile is unavailable");
  }
  if (transform != nullptr) {
    cmsDeleteTransform(transform);
  }
  transform =
      cmsCreateTransform(sourceProfile, inputType, targetProfile, TYPE_RGBA_8,
                         cmsGetHeaderRenderingIntent(sourceProfile),
                         copyAlpha ? cmsFLAGS_COPY_ALPHA : 0);
  cmsCloseProfile(sourceProfile);
  if (transform == nullptr) {
    throw std::runtime_error("Failed to create incremental color transform");
  }
  forceOpaqueOutput = forceOpaque;
}

void IncrementalColorTransform::apply(const uint8_t* input, uint8_t* output,
                                      uint32_t pixelCount) const {
  if (transform == nullptr) {
    throw std::runtime_error("Incremental color transform is not configured");
  }
  cmsDoTransform(transform, input, output, pixelCount);
  if (forceOpaqueOutput) {
    for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
      output[pixel * 4 + 3] = 255;
    }
  }
}
