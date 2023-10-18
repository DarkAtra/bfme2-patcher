package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.padding
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun SmallSurface(
    modifier: Modifier = Modifier,
    content: @Composable (() -> Unit)
) = Surface(
    shape = MaterialTheme.shapes.small,
    modifier = modifier,
    content = {
        Box(
            modifier = Modifier
                .padding(
                    PaddingValues(
                        start = 6.dp,
                        end = 6.dp,
                        top = 6.dp,
                        bottom = 6.dp
                    )
                )
        ) {
            content()
        }
    }
)
