package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
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

const val INDETERMINATE_PROGRESS = -1f

@Composable
fun ProgressBar(
    progress: Float,
    text: String,
) {

    Box(
        modifier = Modifier.height(30.dp)
    ) {

        val color = MaterialTheme.colors.secondary
        val backgroundColor = MaterialTheme.colors.primary
        val modifiers = Modifier.fillMaxSize().clip(RoundedCornerShape(4.dp))

        if (progress == INDETERMINATE_PROGRESS) {
            LinearProgressIndicator(
                backgroundColor = backgroundColor,
                color = color,
                modifier = modifiers,
            )
        } else {
            LinearProgressIndicator(
                backgroundColor = backgroundColor,
                color = color,
                modifier = modifiers,
                progress = progress
            )
        }

        Text(text = text, fontSize = 14.sp, modifier = Modifier.fillMaxHeight().align(Alignment.Center))
    }
}
