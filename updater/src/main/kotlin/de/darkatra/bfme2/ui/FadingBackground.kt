package de.darkatra.bfme2.ui

import androidx.compose.animation.Crossfade
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import kotlinx.coroutines.delay
import org.jetbrains.compose.resources.DrawableResource
import org.jetbrains.compose.resources.painterResource
import java.time.Duration
import kotlin.random.Random

@Composable
fun FadingBackground(
    images: Array<DrawableResource>,
    transitionDelay: Duration,
    transitionDuration: Duration,
    randomizeStartingImage: Boolean = true,
    content: @Composable BoxScope.() -> Unit
) {

    val (currentImage, setCurrentImage) = remember {
        mutableStateOf(
            when (randomizeStartingImage) {
                true -> Random.nextInt(images.size)
                false -> 0
            }
        )
    }

    Box(modifier = Modifier.fillMaxSize()) {
        Crossfade(
            targetState = images[currentImage],
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

    LaunchedEffect(currentImage) {
        delay(transitionDelay.toMillis())
        setCurrentImage((currentImage + 1) % images.size)
    }
}
