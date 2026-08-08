#include "incremental/jpeg_decoder.h"

#include "cmyk.h"
#include "incremental/color_transform.h"
#include "incremental/image_canvas.h"
#include "jpeglib.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

class IncrementalJpegDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalJpegDecoder(const IncrementalDecodeOptionsNative& options)
      : options(options), colorTransform(options) {
    decompressor.err = jpeg_std_error(&errorManager);
    errorManager.error_exit = [](j_common_ptr info) {
      char message[JMSG_LENGTH_MAX];
      info->err->format_message(info, message);
      throw std::runtime_error(message);
    };
    jpeg_create_decompress(&decompressor);
    source.manager.init_source = [](j_decompress_ptr) {};
    source.manager.fill_input_buffer = [](j_decompress_ptr info) {
      auto* input = reinterpret_cast<SuspendingSource*>(info->src);
      if (!input->endOfInput) {
        return static_cast<boolean>(FALSE);
      }
      input->injectedEndMarker = true;
      input->manager.next_input_byte = input->endMarker;
      input->manager.bytes_in_buffer = sizeof(input->endMarker);
      return static_cast<boolean>(TRUE);
    };
    source.manager.skip_input_data = [](j_decompress_ptr info, long count) {
      auto* input = reinterpret_cast<SuspendingSource*>(info->src);
      if (count <= 0) {
        return;
      }
      const size_t requested = static_cast<size_t>(count);
      if (requested <= input->manager.bytes_in_buffer) {
        input->manager.next_input_byte += requested;
        input->manager.bytes_in_buffer -= requested;
      } else {
        input->pendingSkip = requested - input->manager.bytes_in_buffer;
        input->manager.next_input_byte = nullptr;
        input->manager.bytes_in_buffer = 0;
      }
    };
    source.manager.resync_to_restart = jpeg_resync_to_restart;
    source.manager.term_source = [](j_decompress_ptr) {};
    source.manager.bytes_in_buffer = 0;
    source.manager.next_input_byte = nullptr;
    decompressor.src = &source.manager;
    jpeg_save_markers(&decompressor, JPEG_APP0 + 2, 0xFFFF);
  }

  ~IncrementalJpegDecoder() override { jpeg_destroy_decompress(&decompressor); }

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    source.append(bytes, size, endOfInput);
    process(sink);
    publishSnapshot(sink);
    if (endOfInput && source.injectedEndMarker) {
      throw std::runtime_error("Truncated JPEG input");
    }
    if (complete) {
      sink(makeInfoUpdate(IncrementalUpdateType::Complete));
      return IncrementalBackendResult::Complete;
    }
    return IncrementalBackendResult::Accepted;
  }

private:
  struct SuspendingSource {
    jpeg_source_mgr manager{};
    std::vector<uint8_t> buffer;
    size_t pendingSkip = 0;
    bool endOfInput = false;
    bool injectedEndMarker = false;
    JOCTET endMarker[2] = {0xFF, JPEG_EOI};

    void append(const uint8_t* bytes, size_t size, bool end) {
      std::vector<uint8_t> next;
      if (manager.bytes_in_buffer > 0 && manager.next_input_byte != endMarker) {
        next.insert(next.end(), manager.next_input_byte,
                    manager.next_input_byte + manager.bytes_in_buffer);
      }

      const size_t skipped = std::min(pendingSkip, size);
      pendingSkip -= skipped;
      if (skipped > 0) {
        bytes += skipped;
        size -= skipped;
      }
      if (size > 0) {
        next.insert(next.end(), bytes, bytes + size);
      }
      buffer = std::move(next);
      manager.next_input_byte = buffer.empty() ? nullptr : buffer.data();
      manager.bytes_in_buffer = buffer.size();
      endOfInput = end;
    }
  };

  enum class State {
    ReadingHeader,
    Starting,
    BaselineRows,
    ProgressiveReady,
    ProgressiveRows,
    ProgressiveFinishing,
    Finishing,
    Complete,
  };

  void process(const IncrementalUpdateSink& sink) {
    while (true) {
      switch (state) {
      case State::ReadingHeader:
        if (jpeg_read_header(&decompressor, TRUE) == JPEG_SUSPENDED) {
          return;
        }
        configure(sink);
        state = State::Starting;
        break;
      case State::Starting:
        if (!jpeg_start_decompress(&decompressor)) {
          return;
        }
        state = progressive ? State::ProgressiveReady : State::BaselineRows;
        break;
      case State::BaselineRows:
        if (!readRows()) {
          return;
        }
        state = State::Finishing;
        break;
      case State::ProgressiveReady:
        if (!startProgressivePass()) {
          return;
        }
        state = State::ProgressiveRows;
        break;
      case State::ProgressiveRows:
        if (!readRows()) {
          return;
        }
        state = State::ProgressiveFinishing;
        break;
      case State::ProgressiveFinishing:
        if (!jpeg_finish_output(&decompressor)) {
          return;
        }
        lastOutputScan = decompressor.output_scan_number;
        if (jpeg_input_complete(&decompressor) &&
            lastOutputScan == decompressor.input_scan_number) {
          state = State::Finishing;
        } else {
          state = State::ProgressiveReady;
        }
        break;
      case State::Finishing:
        if (!jpeg_finish_decompress(&decompressor)) {
          return;
        }
        state = State::Complete;
        complete = true;
        return;
      case State::Complete:
        return;
      }
    }
  }

  void configure(const IncrementalUpdateSink& sink) {
    const uint32_t sourceWidth = decompressor.image_width;
    const uint32_t sourceHeight = decompressor.image_height;
    outputDimensions = calculate_incremental_output_dimensions(
        sourceWidth, sourceHeight, options);

    decompressor.scale_num = 1;
    decompressor.scale_denom = 1;
    for (const unsigned int denominator : {2U, 4U, 8U}) {
      decompressor.scale_denom = denominator;
      jpeg_calc_output_dimensions(&decompressor);
      if (decompressor.output_width < outputDimensions.width ||
          decompressor.output_height < outputDimensions.height) {
        decompressor.scale_denom = denominator / 2;
        break;
      }
    }
    jpeg_calc_output_dimensions(&decompressor);

    configureColorTransform();
    progressive = decompressor.progressive_mode == TRUE;
    decompressor.buffered_image = progressive ? TRUE : FALSE;
    nativeRow.resize(static_cast<size_t>(decompressor.output_width) *
                     inputComponents);
    rgbaRow.resize(static_cast<size_t>(decompressor.output_width) * 4);
    canvas = std::make_unique<IncrementalImageCanvas>(
        decompressor.output_width, decompressor.output_height,
        outputDimensions);
    sink(makeInfoUpdate(IncrementalUpdateType::MetadataAvailable));
  }

  void configureColorTransform() {
    JOCTET* profileBytes = nullptr;
    unsigned int profileSize = 0;
    cmsHPROFILE sourceProfile = nullptr;
    if (jpeg_read_icc_profile(&decompressor, &profileBytes, &profileSize)) {
      sourceProfile = cmsOpenProfileFromMem(profileBytes, profileSize);
      std::free(profileBytes);
    }

    const bool cmyk = decompressor.jpeg_color_space == JCS_CMYK ||
                      decompressor.jpeg_color_space == JCS_YCCK;
    const bool grayscale = decompressor.jpeg_color_space == JCS_GRAYSCALE;
    if (sourceProfile != nullptr) {
      const cmsColorSpaceSignature space = cmsGetColorSpace(sourceProfile);
      if ((cmyk && space != cmsSigCmykData) ||
          (grayscale && space != cmsSigGrayData) ||
          (!cmyk && !grayscale && space != cmsSigRgbData)) {
        cmsCloseProfile(sourceProfile);
        sourceProfile = nullptr;
      }
    }

    if (cmyk) {
      if (sourceProfile == nullptr) {
        sourceProfile = cmsOpenProfileFromMem(CMYK_USWebCoatedSWOP_icc,
                                              CMYK_USWebCoatedSWOP_icc_len);
      }
      inputType = decompressor.saw_Adobe_marker ? TYPE_CMYK_8_REV : TYPE_CMYK_8;
      inputComponents = 4;
      decompressor.out_color_space = JCS_CMYK;
      colorTransform.configure(sourceProfile, inputType, false, true);
      return;
    }

    if (grayscale && sourceProfile != nullptr) {
      inputType = TYPE_GRAY_8;
      inputComponents = 1;
      decompressor.out_color_space = JCS_GRAYSCALE;
      colorTransform.configure(sourceProfile, inputType, false, true);
      return;
    }

    if (sourceProfile == nullptr) {
      sourceProfile = cmsCreate_sRGBProfile();
    }
    inputType = TYPE_RGBA_8;
    inputComponents = 4;
    decompressor.out_color_space = JCS_EXT_RGBA;
    colorTransform.configure(sourceProfile, inputType, true, false);
  }

  bool startProgressivePass() {
    int consumeResult;
    do {
      consumeResult = jpeg_consume_input(&decompressor);
    } while (consumeResult != JPEG_SUSPENDED &&
             consumeResult != JPEG_REACHED_EOI);

    if (decompressor.input_scan_number <= lastOutputScan &&
        !jpeg_input_complete(&decompressor)) {
      return false;
    }
    return jpeg_start_output(&decompressor, decompressor.input_scan_number) ==
           TRUE;
  }

  bool readRows() {
    JSAMPROW row = nativeRow.data();
    while (decompressor.output_scanline < decompressor.output_height) {
      const JDIMENSION sourceY = decompressor.output_scanline;
      if (jpeg_read_scanlines(&decompressor, &row, 1) == 0) {
        return false;
      }
      colorTransform.apply(nativeRow.data(), rgbaRow.data(),
                           decompressor.output_width);
      canvas->updateSourceRow(sourceY, rgbaRow.data());
    }
    return true;
  }

  IncrementalUpdate makeInfoUpdate(IncrementalUpdateType type) const {
    IncrementalUpdate update{
        .type = type,
        .format = static_cast<int32_t>(ImageFormat::Jpeg),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.info = std::make_unique<IncrementalImageInfoNative>(
        IncrementalImageInfoNative{.format = ImageFormat::Jpeg,
                                   .width = decompressor.image_width,
                                   .height = decompressor.image_height,
                                   .outputWidth = outputDimensions.width,
                                   .outputHeight = outputDimensions.height,
                                   .isAnimated = false,
                                   .hasAlpha = false});
    return update;
  }

  void publishSnapshot(const IncrementalUpdateSink& sink) {
    if (canvas == nullptr) {
      return;
    }
    auto snapshot = canvas->takeSnapshot();
    if (snapshot == nullptr) {
      return;
    }
    IncrementalUpdate update{
        .type = IncrementalUpdateType::StillImageAvailable,
        .format = static_cast<int32_t>(ImageFormat::Jpeg),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.snapshot = std::move(snapshot);
    sink(std::move(update));
  }

  IncrementalDecodeOptionsNative options;
  IncrementalColorTransform colorTransform;
  jpeg_decompress_struct decompressor{};
  jpeg_error_mgr errorManager{};
  SuspendingSource source{};
  State state = State::ReadingHeader;
  IncrementalOutputDimensions outputDimensions{};
  std::unique_ptr<IncrementalImageCanvas> canvas;
  std::vector<uint8_t> nativeRow;
  std::vector<uint8_t> rgbaRow;
  cmsUInt32Number inputType = TYPE_RGBA_8;
  uint32_t inputComponents = 4;
  int lastOutputScan = 0;
  bool progressive = false;
  bool complete = false;
};

} // namespace

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_jpeg_decoder(const IncrementalDecodeOptionsNative& options) {
  return std::make_unique<IncrementalJpegDecoder>(options);
}
