package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.material.ButtonDefaults
import androidx.compose.material.MaterialTheme
import androidx.compose.material.ProvideTextStyle
import androidx.compose.material.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun SmallSurface(
    modifier: Modifier = Modifier,
    content: @Composable (RowScope.() -> Unit)
) = Surface(
    shape = MaterialTheme.shapes.small,
    color = ButtonDefaults.buttonColors().backgroundColor(true).value,
    contentColor = ButtonDefaults.buttonColors().contentColor(true).value,
    modifier = Modifier.height(32.dp).wrapContentHeight(Alignment.CenterVertically).then(modifier),
    content = {
        ProvideTextStyle(
            value = MaterialTheme.typography.button
        ) {
            Row(
                Modifier
                    .defaultMinSize(
                        minWidth = ButtonDefaults.MinWidth,
                        minHeight = ButtonDefaults.MinHeight
                    )
                    .padding(
                        PaddingValues(
                            start = 8.dp,
                            end = 8.dp
                        )
                    ),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically,
                content = content
            )
        }
    }
)
