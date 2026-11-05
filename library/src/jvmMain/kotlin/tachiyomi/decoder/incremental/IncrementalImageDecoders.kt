package tachiyomi.decoder.incremental

import tachiyomi.decoder.PixelAllocator
import tachiyomi.decoder.PixelTarget

/** Creates an incremental decoder that publishes its pixels into targets [allocator] provides. */
fun <T : PixelTarget> IncrementalImageDecoder.Companion.newInstance(
  allocator: PixelAllocator<T>,
  options: IncrementalDecodeOptions = IncrementalDecodeOptions(),
): IncrementalImageDecoder<T> = create(options, allocator)
