package de.darkatra.bfme2.ui

import java.awt.image.BufferedImage
import java.io.File
import javax.imageio.ImageIO
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class NinePatchButtonAssetsTest {

    private val drawableDirectory = File("src/main/composeResources/drawable")

    @Test
    fun `button textures are aligned to the nine-patch grid`() {

        buttonTextureNames.forEach { textureName ->
            val image = ImageIO.read(drawableDirectory.resolve(textureName))

            assertEquals(400, image.width, textureName)
            assertEquals(100, image.height, textureName)
            assertEquals(0, (image.width - CAP_INSET_LEFT_PX - CAP_INSET_RIGHT_PX) % ARTWORK_GRID_PX, textureName)
            assertEquals(0, (image.height - CAP_INSET_TOP_PX - CAP_INSET_BOTTOM_PX) % ARTWORK_GRID_PX, textureName)
        }
    }

    @Test
    fun `button textures preserve transparent corners without shadow padding`() {

        buttonTextureNames.forEach { textureName ->
            val image = ImageIO.read(drawableDirectory.resolve(textureName))
            val cornerAlphaValues = intArrayOf(
                image.getRGB(0, 0).ushr(24),
                image.getRGB(image.width - 1, 0).ushr(24),
                image.getRGB(0, image.height - 1).ushr(24),
                image.getRGB(image.width - 1, image.height - 1).ushr(24),
            )

            assertTrue(image.colorModel.hasAlpha(), textureName)
            assertTrue(cornerAlphaValues.all { it == 0 }, textureName)
            assertTrue((0 until image.width).any { image.getRGB(it, 0).ushr(24) > 0 }, textureName)
            assertTrue((0 until image.width).any { image.getRGB(it, image.height - 1).ushr(24) > 0 }, textureName)
            assertTrue((0 until image.height).any { image.getRGB(0, it).ushr(24) > 0 }, textureName)
            assertTrue((0 until image.height).any { image.getRGB(image.width - 1, it).ushr(24) > 0 }, textureName)
        }
    }

    @Test
    fun `button states use distinct artwork with matching dimensions`() {

        val defaultImage = ImageIO.read(drawableDirectory.resolve(DEFAULT_TEXTURE))
        val defaultAlpha = defaultImage.alphaValues()
        val defaultColors = defaultImage.rgbValues()

        stateTextureNames.forEach { textureName ->
            val stateImage = ImageIO.read(drawableDirectory.resolve(textureName))

            assertEquals(defaultImage.width, stateImage.width, textureName)
            assertEquals(defaultImage.height, stateImage.height, textureName)
            assertContentEquals(defaultAlpha, stateImage.alphaValues(), textureName)
            assertNotEquals(defaultColors, stateImage.rgbValues(), textureName)
        }
    }

    @Test
    fun `button textures are greyscale so they can be tinted`() {

        buttonTextureNames.forEach { textureName ->
            val image = ImageIO.read(drawableDirectory.resolve(textureName))

            assertTrue(
                image.rgbValues().all { color ->
                    val red = color shr 16 and 0xff
                    val green = color shr 8 and 0xff
                    val blue = color and 0xff
                    red == green && green == blue
                },
                textureName,
            )
        }
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

    private companion object {
        const val CAP_INSET_LEFT_PX = 80
        const val CAP_INSET_TOP_PX = 30
        const val CAP_INSET_RIGHT_PX = 80
        const val CAP_INSET_BOTTOM_PX = 30
        const val ARTWORK_GRID_PX = 8
        const val DEFAULT_TEXTURE = "button_9.png"

        val stateTextureNames = listOf(
            "button_hover_9.png",
            "button_pressed_9.png",
            "button_disabled_9.png",
        )
        val buttonTextureNames = listOf(
            DEFAULT_TEXTURE,
            *stateTextureNames.toTypedArray(),
        )
    }
}
