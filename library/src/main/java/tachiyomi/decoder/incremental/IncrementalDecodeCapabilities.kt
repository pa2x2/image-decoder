package tachiyomi.decoder.incremental

data class IncrementalDecodeCapabilities(
  val stillImageUpdates: Boolean,
  val animationFrames: Boolean,
)
