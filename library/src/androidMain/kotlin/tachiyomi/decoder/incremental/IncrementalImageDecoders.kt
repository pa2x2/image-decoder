package tachiyomi.decoder.incremental

import android.graphics.Bitmap
import tachiyomi.decoder.BitmapAllocator

/** Creates an incremental decoder that publishes its pixels as bitmaps. */
fun IncrementalImageDecoder.Companion.newInstance(
  options: IncrementalDecodeOptions = IncrementalDecodeOptions(),
): IncrementalImageDecoder<Bitmap> = create(options, BitmapAllocator)
