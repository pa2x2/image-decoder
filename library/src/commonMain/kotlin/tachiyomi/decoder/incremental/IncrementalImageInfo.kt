package tachiyomi.decoder.incremental

import tachiyomi.decoder.Format

data class IncrementalImageInfo(
  val format: Format,
  val width: Int,
  val height: Int,
  val outputWidth: Int,
  val outputHeight: Int,
  val isAnimated: Boolean,
  val hasAlpha: Boolean,
  val frameCount: Int?,
  val loopCount: Int?,
)
