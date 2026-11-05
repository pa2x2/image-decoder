package tachiyomi.decoder.incremental

import tachiyomi.decoder.ImageRegion

data class IncrementalAnimationFrame(
  val index: Int,
  val durationMillis: Long,
  val updatedRegion: ImageRegion,
  val blendOperation: IncrementalBlendOperation,
  val disposalOperation: IncrementalDisposalOperation,
)
