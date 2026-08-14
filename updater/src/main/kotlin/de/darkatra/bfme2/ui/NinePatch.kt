package de.darkatra.bfme2.ui

import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.graphics.ColorFilter
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
    pixelScale: Float = 1f,
    filterQuality: FilterQuality = FilterQuality.Low,
    colorFilter: ColorFilter? = null,
): Modifier = drawWithCache {

    val srcW = image.width
    val srcH = image.height

    require(pixelScale.isFinite() && pixelScale > 0f)
    require(insets.left + insets.right <= srcW)
    require(insets.top + insets.bottom <= srcH)

    val dstW = size.width.roundToInt()
    val dstH = size.height.roundToInt()

    val sx = intArrayOf(0, insets.left, srcW - insets.right, srcW)
    val sy = intArrayOf(0, insets.top, srcH - insets.bottom, srcH)

    val dx = calculateNinePatchDestinationAxis(
        destinationSize = dstW,
        startInset = insets.left,
        endInset = insets.right,
        pixelScale = pixelScale,
        density = density,
    )
    val dy = calculateNinePatchDestinationAxis(
        destinationSize = dstH,
        startInset = insets.top,
        endInset = insets.bottom,
        pixelScale = pixelScale,
        density = density,
    )

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
                    colorFilter = colorFilter,
                )
            }
        }
    }
}

internal fun calculateNinePatchDestinationAxis(
    destinationSize: Int,
    startInset: Int,
    endInset: Int,
    pixelScale: Float,
    density: Float,
): IntArray {
    val destinationScale = density / pixelScale
    val fixedSize = (startInset + endInset) * destinationScale
    val fixedScale = when {
        fixedSize > destinationSize && fixedSize > 0 -> destinationSize.toFloat() / fixedSize
        else -> 1f
    }
    val destinationStart = (startInset * destinationScale * fixedScale).roundToInt()
    val destinationEnd = (endInset * destinationScale * fixedScale).roundToInt()

    return intArrayOf(0, destinationStart, destinationSize - destinationEnd, destinationSize)
}
