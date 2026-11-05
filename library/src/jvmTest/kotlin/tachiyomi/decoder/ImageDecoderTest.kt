package tachiyomi.decoder

import java.awt.image.BufferedImage
import java.io.ByteArrayOutputStream
import javax.imageio.ImageIO
import kotlin.math.abs
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class ImageDecoderTest {

  /**
   * The samples are a 16x16 image of four 8x8 quadrants, red, green, blue and white, written by ImageMagick, cjxl and
   * avifenc. A desktop build that leaves a codec out, or links it wrongly, fails here.
   */
  @Test
  fun `every supported format decodes on the desktop`() {
    for (sample in listOf("png", "jpg", "webp", "jxl", "avif", "heic")) {
      val decoder = assertNotNull(
        ImageDecoder.newInstance(javaClass.getResourceAsStream("/samples/quadrants.$sample")!!),
        "no decoder for $sample",
      )
      assertEquals(16 to 16, decoder.width to decoder.height, sample)
      decoder.decode(allocator = TestPixels.allocator)!!.use { pixels ->
        assertColor(listOf(255, 0, 0, 255), pixels.rgba(3, 3), sample)
        assertColor(listOf(0, 255, 0, 255), pixels.rgba(12, 3), sample)
        assertColor(listOf(0, 0, 255, 255), pixels.rgba(3, 12), sample)
        assertColor(listOf(255, 255, 255, 255), pixels.rgba(12, 12), sample)
      }
      decoder.recycle()
    }
  }

  @Test
  fun `a sampled region is written premultiplied at the target's row stride`() {
    // 8x4: the left half opaque red, the right half green at alpha 128.
    val image = BufferedImage(8, 4, BufferedImage.TYPE_INT_ARGB)
    for (y in 0 until 4) {
      for (x in 0 until 8) image.setRGB(x, y, if (x < 4) 0xffff0000.toInt() else 0x8000ff00.toInt())
    }
    val decoder = assertNotNull(ImageDecoder.newInstance(png(image).inputStream()))

    // The middle 4x4, halved: one column of each half, two rows.
    decoder.decode(ImageRegion(2, 0, 6, 4), sampleSize = 2, allocator = TestPixels.allocator)!!.use { pixels ->
      assertEquals(2 to 2, pixels.width to pixels.height)
      for (y in 0 until 2) {
        assertColor(listOf(255, 0, 0, 255), pixels.rgba(0, y), "row $y, red")
        assertColor(listOf(0, 128, 0, 128), pixels.rgba(1, y), "row $y, green")
      }
    }
    decoder.recycle()
  }

  @Test
  fun `cropped borders shrink the bounds regions are relative to`() {
    // A 4x2 dark block at (6, 3) on a 16x8 white page.
    val image = BufferedImage(16, 8, BufferedImage.TYPE_INT_RGB)
    for (y in 0 until 8) {
      for (x in 0 until 16) {
        image.setRGB(x, y, if (x in 6 until 10 && y in 3 until 5) 0x202020 else 0xffffff)
      }
    }
    val decoder = assertNotNull(ImageDecoder.newInstance(png(image).inputStream(), cropBorders = true))

    assertEquals(16 to 8, decoder.sourceWidth to decoder.sourceHeight)
    assertEquals(ImageRegion(6, 3, 10, 5), decoder.bounds)
    decoder.decode(allocator = TestPixels.allocator)!!.use { pixels ->
      assertColor(listOf(32, 32, 32, 255), pixels.rgba(0, 0), "cropped origin")
    }
    decoder.recycle()
  }

  private fun png(image: BufferedImage): ByteArray =
    ByteArrayOutputStream().also { ImageIO.write(image, "png", it) }.toByteArray()

  /** Lossy samples may drift a few levels from the colour they were encoded from. */
  private fun assertColor(expected: List<Int>, actual: List<Int>, message: String) {
    assertTrue(
      expected.zip(actual).all { (e, a) -> abs(e - a) <= 6 },
      "$message: expected $expected, was $actual",
    )
  }
}
