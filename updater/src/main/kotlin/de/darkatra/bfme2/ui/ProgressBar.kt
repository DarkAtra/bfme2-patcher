package de.darkatra.bfme2.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.progressSemantics
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import de.darkatra.bfme2.updater.generated.resources.Res
import de.darkatra.bfme2.updater.generated.resources.RingbearerMedium
import de.darkatra.bfme2.updater.generated.resources.progress_bar_9
import de.darkatra.bfme2.updater.generated.resources.progress_indicator_9
import org.jetbrains.compose.resources.Font
import org.jetbrains.compose.resources.imageResource
import kotlin.math.roundToInt

const val INDETERMINATE_PROGRESS = -1f
private const val INDETERMINATE_INDICATOR_FRACTION = 0.35f

@Composable
fun ProgressBar(
    progress: Float,
    text: String,
    tint: Color = Color.White,
) {

    val frameTexture = imageResource(Res.drawable.progress_bar_9)
    val indicatorTexture = imageResource(Res.drawable.progress_indicator_9)
    val fontFamily = FontFamily(Font(Res.font.RingbearerMedium))
    val indicatorModifier = Modifier
        .fillMaxSize()
        .padding(horizontal = 10.dp, vertical = 5.dp)
        .clip(RoundedCornerShape(15.dp))

    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier.height(40.dp),
    ) {

        TexturedProgressIndicator(
            progress = progress,
            texture = indicatorTexture,
            tint = tint,
            modifier = indicatorModifier,
        )

        Box(
            modifier = Modifier
                .fillMaxSize()
                .ninePatch(
                    image = frameTexture,
                    insets = NinePatchInsets(
                        left = 80,
                        top = 20,
                        right = 80,
                        bottom = 20,
                    ),
                    pixelScale = 2f,
                    colorFilter = ColorFilter.tint(tint, BlendMode.Modulate),
                )
        )

        Text(
            text = text,
            maxLines = 1,
            style = progressBarTextStyle(tint).copy(fontFamily = fontFamily),
        )
    }
}

@Composable
private fun TexturedProgressIndicator(
    progress: Float,
    texture: ImageBitmap,
    tint: Color,
    modifier: Modifier = Modifier,
) {

    val indicatorColor = lerp(Color.Black, tint.copy(alpha = 1f), 0.6f)
    val colorFilter = ColorFilter.tint(indicatorColor, BlendMode.Modulate)
    val trackColor = progressBarTrackColor(tint)
    val semanticsModifier = when {
        progress == INDETERMINATE_PROGRESS -> Modifier.progressSemantics()
        else -> Modifier.progressSemantics(progressBarIndicatorFraction(progress))
    }

    BoxWithConstraints(
        modifier = modifier
            .then(semanticsModifier)
            .background(trackColor)
            .texturedProgressIndicator(
                texture = texture,
                colorFilter = ColorFilter.tint(trackColor, BlendMode.Modulate),
            ),
    ) {
        if (progress == INDETERMINATE_PROGRESS) {
            val transition = rememberInfiniteTransition()
            val animationProgress by transition.animateFloat(
                initialValue = 0f,
                targetValue = 1f,
                animationSpec = infiniteRepeatable(
                    animation = tween(durationMillis = 1_200, easing = LinearEasing),
                    repeatMode = RepeatMode.Restart,
                ),
            )
            val trackWidth = with(LocalDensity.current) { maxWidth.toPx() }
            val indicatorWidth = trackWidth * INDETERMINATE_INDICATOR_FRACTION

            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(INDETERMINATE_INDICATOR_FRACTION)
                    .offset {
                        IntOffset(
                            x = ((trackWidth + indicatorWidth) * animationProgress - indicatorWidth).roundToInt(),
                            y = 0,
                        )
                    }
                    .texturedProgressIndicator(texture, colorFilter)
            )
        } else {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(progressBarIndicatorFraction(progress))
                    .texturedProgressIndicator(texture, colorFilter)
            )
        }
    }
}

private fun Modifier.texturedProgressIndicator(
    texture: ImageBitmap,
    colorFilter: ColorFilter,
): Modifier = ninePatch(
    image = texture,
    insets = NinePatchInsets(
        left = 16,
        top = 16,
        right = 16,
        bottom = 16,
    ),
    pixelScale = 2f,
    colorFilter = colorFilter,
)

internal fun progressBarIndicatorFraction(progress: Float): Float {
    return when {
        !progress.isFinite() -> 0f
        else -> progress.coerceIn(0f, 1f)
    }
}

internal fun progressBarTrackColor(tint: Color): Color = lerp(
    start = Color.Black,
    stop = tint.copy(alpha = 1f),
    fraction = 0.35f,
)

internal fun progressBarTextStyle(tint: Color): TextStyle = TextStyle(
    color = buttonTextColor(tint, enabled = true),
    fontSize = 15.sp,
    fontWeight = FontWeight.W400,
    lineHeight = 15.sp,
    textAlign = TextAlign.Center,
)
