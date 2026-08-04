package tachiyomi.decoder.incremental

data class IncrementalAnimationFrame(
  val index: Int,
  val durationMillis: Long,
  val updatedRegion: IncrementalImageRegion,
  val blendOperation: IncrementalBlendOperation,
  val disposalOperation: IncrementalDisposalOperation,
)
