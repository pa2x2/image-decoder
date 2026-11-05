package tachiyomi.decoder.incremental

import tachiyomi.decoder.TestPixels
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull

class IncrementalImageDecoderTest {

  @Test
  fun `still images are published into the caller's targets`() {
    val bytes = javaClass.getResourceAsStream("/samples/quadrants.png")!!.readBytes()
    val updates = IncrementalImageDecoder.newInstance(TestPixels.allocator).use { decoder ->
      decoder.append(bytes, endOfInput = true)
      generateSequence { decoder.pollUpdate() }.toList()
    }

    val stills = updates.filterIsInstance<IncrementalDecodeUpdate.StillImageAvailable<TestPixels>>()
    try {
      val pixels = assertNotNull(stills.lastOrNull()).pixels
      assertEquals(16 to 16, pixels.width to pixels.height)
      assertEquals(listOf(0, 0, 255, 255), pixels.rgba(3, 12))
      assertNotNull(updates.filterIsInstance<IncrementalDecodeUpdate.Complete>().singleOrNull())
    } finally {
      stills.forEach { it.pixels.close() }
    }
  }
}
