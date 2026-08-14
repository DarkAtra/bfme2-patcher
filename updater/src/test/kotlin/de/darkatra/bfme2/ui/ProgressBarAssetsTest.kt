package de.darkatra.bfme2.ui

import java.awt.image.BufferedImage
import java.io.File
import javax.imageio.ImageIO
import kotlin.math.abs
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class ProgressBarAssetsTest {

    private val textureFile = File("src/main/composeResources/drawable/progress_bar_9.png")
    private val indicatorTextureFile = File("src/main/composeResources/drawable/progress_indicator_9.png")

    @Test
    fun `progress bar texture is aligned to the nine-patch grid`() {

        val image = assertNotNull(ImageIO.read(textureFile))

        assertEquals(400, image.width)
        assertEquals(80, image.height)
        assertEquals(0, (image.width - CAP_INSET_LEFT_PX - CAP_INSET_RIGHT_PX) % ARTWORK_GRID_PX)
        assertEquals(0, (image.height - CAP_INSET_TOP_PX - CAP_INSET_BOTTOM_PX) % ARTWORK_GRID_PX)
    }

    @Test
    fun `progress bar texture is transparent and greyscale so it can be tinted`() {

        val image = assertNotNull(ImageIO.read(textureFile))

        assertTrue(image.colorModel.hasAlpha())
        assertTrue(image.alphaValues().any { it == 0 })
        assertTrue(image.alphaValues().any { it > 0 })
        assertTrue(
            image.rgbValues().all { color ->
                val red = color shr 16 and 0xff
                val green = color shr 8 and 0xff
                val blue = color and 0xff
                red == green && green == blue
            }
        )
    }

    @Test
    fun `progress bar has balanced rounded terminal ornaments`() {

        val image = assertNotNull(ImageIO.read(textureFile))
        val leftRing = image.brightnessValues(LEFT_TERMINAL_RING_POINTS)
        val rightRing = image.brightnessValues(
            LEFT_TERMINAL_RING_POINTS.map { (x, y) -> image.width - 1 - x to y }
        )

        val leftHighlightCount = leftRing.count { it >= TERMINAL_HIGHLIGHT }
        val rightHighlightCount = rightRing.count { it >= TERMINAL_HIGHLIGHT }

        assertTrue(
            leftHighlightCount >= 5,
            "Expected swept highlights like the button end caps, but found $leftHighlightCount highlighted points: $leftRing",
        )
        assertTrue(abs(leftHighlightCount - rightHighlightCount) <= 1)
    }

    @Test
    fun `progress indicator has a tintable textured nine-patch asset`() {

        val image = assertNotNull(ImageIO.read(indicatorTextureFile))

        assertEquals(128, image.width)
        assertEquals(48, image.height)
        assertTrue(image.colorModel.hasAlpha())
        assertTrue(image.alphaValues().any { it == 0 })
        assertTrue(image.alphaValues().any { it == 0xff })
        assertEquals(0, image.getRGB(0, 0).ushr(24))
        assertEquals(0xff, image.getRGB(0, image.height / 2).ushr(24))
        assertTrue(image.rgbValues().all(::isGreyscale))
        assertTrue(image.greyscaleValues().distinct().size >= 32)
    }

    private fun BufferedImage.alphaValues(): IntArray {
        return IntArray(width * height) { index ->
            getRGB(index % width, index / width).ushr(24)
        }
    }

    private fun BufferedImage.rgbValues(): List<Int> {
        return List(width * height) { index ->
            getRGB(index % width, index / width) and 0x00ffffff
        }
    }

    private fun BufferedImage.greyscaleValues(): List<Int> {
        return List(width * height) { index ->
            getRGB(index % width, index / width) and 0xff
        }
    }

    private fun BufferedImage.brightnessValues(points: List<Pair<Int, Int>>): List<Int> {
        return points.map { (x, y) -> getRGB(x, y) and 0xff }
    }

    private fun isGreyscale(color: Int): Boolean {
        val red = color shr 16 and 0xff
        val green = color shr 8 and 0xff
        val blue = color and 0xff
        return red == green && green == blue
    }

    private companion object {
        const val CAP_INSET_LEFT_PX = 80
        const val CAP_INSET_TOP_PX = 20
        const val CAP_INSET_RIGHT_PX = 80
        const val CAP_INSET_BOTTOM_PX = 20
        const val ARTWORK_GRID_PX = 8
        const val TERMINAL_HIGHLIGHT = 50
        val LEFT_TERMINAL_RING_POINTS = listOf(
            8 to 12,
            20 to 8,
            36 to 10,
            50 to 16,
            50 to 64,
            36 to 70,
            20 to 72,
            8 to 68,
        )
    }
}
