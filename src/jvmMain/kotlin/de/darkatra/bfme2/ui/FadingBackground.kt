package de.darkatra.bfme2.ui

import androidx.compose.animation.Crossfade
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.time.Duration

@Composable
fun FadingBackground(
    imagePaths: Array<String>,
    transitionDelay: Duration,
    transitionDuration: Duration,
    content: @Composable BoxScope.() -> Unit
) {

    val animatedBackgroundScope = rememberCoroutineScope()
    val (currentImage, setCurrentImage) = remember { mutableStateOf(0) }

    Box(modifier = Modifier.fillMaxSize()) {
        Crossfade(
            targetState = imagePaths[currentImage],
            animationSpec = tween(transitionDuration.toMillis().toInt())
        ) { image ->
            Image(
                painter = painterResource(image),
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier.fillMaxSize()
            )
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        content()
    }

    animatedBackgroundScope.launch {
        delay(transitionDelay.toMillis())
        setCurrentImage((currentImage + 1) % imagePaths.size)
    }
}
