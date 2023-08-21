package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.height
import androidx.compose.material.Button
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun SmallButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    content: @Composable (RowScope.() -> Unit)
) = Button(
    onClick = onClick,
    modifier = Modifier.height(32.dp).then(modifier),
    contentPadding = PaddingValues(
        start = 8.dp,
        end = 8.dp,
        top = 0.dp,
        bottom = 0.dp
    ),
    enabled = enabled,
    content = content
)
