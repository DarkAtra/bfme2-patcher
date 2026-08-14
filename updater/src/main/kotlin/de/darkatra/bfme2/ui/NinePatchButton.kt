package de.darkatra.bfme2.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsHoveredAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import de.darkatra.bfme2.updater.generated.resources.Res
import de.darkatra.bfme2.updater.generated.resources.button_primary_9slice
import de.darkatra.bfme2.updater.generated.resources.button_secondary_9slice
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

    val defaultTexture = imageResource(Res.drawable.button_secondary_9slice)
    val hoverTexture = imageResource(Res.drawable.button_primary_9slice)
    val activeTexture = imageResource(Res.drawable.button_secondary_9slice)
    val disabledTexture = imageResource(Res.drawable.button_secondary_9slice)

    val texture = when {
        !enabled -> disabledTexture
        pressed -> activeTexture
        hovered -> hoverTexture
        else -> defaultTexture
    }

    Box(
        modifier = modifier
            .size(320.dp, 80.dp)
            .ninePatch(
                image = texture,
                insets = NinePatchInsets(
                    left = 48,
                    top = 48,
                    right = 48,
                    bottom = 48,
                ),
            )
            .clickable(
                enabled = enabled,
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick,
            ),
        content = content,
    )
}
