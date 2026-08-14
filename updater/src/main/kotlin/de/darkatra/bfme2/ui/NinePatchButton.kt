package de.darkatra.bfme2.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsHoveredAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.material.LocalContentColor
import androidx.compose.material.ProvideTextStyle
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import de.darkatra.bfme2.updater.generated.resources.Res
import de.darkatra.bfme2.updater.generated.resources.RingbearerMedium
import de.darkatra.bfme2.updater.generated.resources.button_9
import de.darkatra.bfme2.updater.generated.resources.button_disabled_9
import de.darkatra.bfme2.updater.generated.resources.button_hover_9
import de.darkatra.bfme2.updater.generated.resources.button_pressed_9
import org.jetbrains.compose.resources.Font
import org.jetbrains.compose.resources.imageResource

@Composable
fun NinePatchButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    content: @Composable (BoxScope.() -> Unit)
) {

    val interactionSource = remember {
        MutableInteractionSource()
    }

    val hovered by interactionSource.collectIsHoveredAsState()
    val pressed by interactionSource.collectIsPressedAsState()

    val defaultTexture = imageResource(Res.drawable.button_9)
    val hoverTexture = imageResource(Res.drawable.button_hover_9)
    val activeTexture = imageResource(Res.drawable.button_pressed_9)
    val disabledTexture = imageResource(Res.drawable.button_disabled_9)

    val fontFamily = FontFamily(Font(Res.font.RingbearerMedium))
    val fontColor = when {
        enabled -> Color(0xFFF1D58A)
        else -> Color(0xFFAAA69A)
    }

    val texture = when {
        !enabled -> disabledTexture
        pressed -> activeTexture
        hovered -> hoverTexture
        else -> defaultTexture
    }

    Box(
        modifier = modifier
            .height(48.dp)
            .ninePatch(
                image = texture,
                insets = NinePatchInsets(
                    left = 80,
                    top = 30,
                    right = 80,
                    bottom = 30,
                ),
                pixelScale = 2f,
            )
            .clickable(
                enabled = enabled,
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick,
            ),
        contentAlignment = Alignment.Center,
    ) {
        Box(modifier = Modifier.offset(y = 2.dp)) {
            CompositionLocalProvider(LocalContentColor provides fontColor) {
                ProvideTextStyle(
                    value = TextStyle(
                        color = fontColor,
                        fontFamily = fontFamily,
                    )
                ) {
                    content()
                }
            }
        }
    }
}
