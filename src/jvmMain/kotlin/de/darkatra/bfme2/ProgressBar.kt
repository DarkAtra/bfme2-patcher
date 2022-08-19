package de.darkatra.bfme2

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.LinearProgressIndicator
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun ProgressBar(
    progress: Float,
    text: String,
    isIntermediate: Boolean
) {

    Box {

        val color = MaterialTheme.colors.secondary
        val backgroundColor = MaterialTheme.colors.primary
        val modifiers = Modifier.fillMaxWidth().height(28.dp).clip(RoundedCornerShape(4.dp))

        if (isIntermediate) {
            LinearProgressIndicator(
                backgroundColor = backgroundColor,
                color = color,
                modifier = modifiers,
            )

            Text(text = text, fontSize = 14.sp, modifier = Modifier.align(Alignment.Center))
        } else {
            LinearProgressIndicator(
                backgroundColor = backgroundColor,
                color = color,
                modifier = modifiers,
                progress = progress
            )

            Text(text = text, fontSize = 14.sp, modifier = Modifier.align(Alignment.Center))
        }
    }
}
