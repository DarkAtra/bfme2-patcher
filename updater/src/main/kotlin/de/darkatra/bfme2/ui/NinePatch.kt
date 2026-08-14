package de.darkatra.bfme2.ui

import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import kotlin.math.roundToInt

data class NinePatchInsets(
    val left: Int,
    val top: Int,
    val right: Int,
    val bottom: Int,
)

fun Modifier.ninePatch(
    image: ImageBitmap,
    insets: NinePatchInsets,
    filterQuality: FilterQuality = FilterQuality.Low,
): Modifier = drawWithCache {

    val srcW = image.width
    val srcH = image.height

    require(insets.left + insets.right <= srcW)
    require(insets.top + insets.bottom <= srcH)

    val dstW = size.width.roundToInt()
    val dstH = size.height.roundToInt()

    val horizontalFixed = insets.left + insets.right
    val verticalFixed = insets.top + insets.bottom

    val horizontalScale = when {
        horizontalFixed > dstW && horizontalFixed > 0 -> dstW.toFloat() / horizontalFixed
        else -> 1f
    }
    val verticalScale = when {
        verticalFixed > dstH && verticalFixed > 0 -> dstH.toFloat() / verticalFixed
        else -> 1f
    }

    val dstLeft = (insets.left * horizontalScale).roundToInt()
    val dstRight = (insets.right * horizontalScale).roundToInt()
    val dstTop = (insets.top * verticalScale).roundToInt()
    val dstBottom = (insets.bottom * verticalScale).roundToInt()

    val sx = intArrayOf(0, insets.left, srcW - insets.right, srcW)
    val sy = intArrayOf(0, insets.top, srcH - insets.bottom, srcH)

    val dx = intArrayOf(0, dstLeft, dstW - dstRight, dstW)
    val dy = intArrayOf(0, dstTop, dstH - dstBottom, dstH)

    onDrawBehind {
        for (y in 0 until 3) {
            for (x in 0 until 3) {

                val srcWidth = sx[x + 1] - sx[x]
                val srcHeight = sy[y + 1] - sy[y]

                val dstWidth = dx[x + 1] - dx[x]
                val dstHeight = dy[y + 1] - dy[y]

                if (srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
                    continue
                }

                drawImage(
                    image = image,
                    srcOffset = IntOffset(sx[x], sy[y]),
                    srcSize = IntSize(srcWidth, srcHeight),
                    dstOffset = IntOffset(dx[x], dy[y]),
                    dstSize = IntSize(dstWidth, dstHeight),
                    filterQuality = filterQuality,
                )
            }
        }
    }
}
