package de.darkatra.bfme2.ui

import androidx.compose.animation.Crossfade
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import kotlinx.coroutines.delay
import org.jetbrains.compose.resources.DrawableResource
import org.jetbrains.compose.resources.painterResource
import kotlin.random.Random
import kotlin.time.Duration

data class BackgroundImage(
    val image: DrawableResource,
    val accentColor: Color,
)

@Composable
fun FadingBackground(
    images: Array<BackgroundImage>,
    transitionDelay: Duration,
    transitionDuration: Duration,
    randomizeStartingImage: Boolean = true,
    content: @Composable BoxScope.(Color) -> Unit
) {

    require(images.isNotEmpty()) {
        "images must not be empty"
    }

    val (currentImage, setCurrentImage) = remember {
        mutableStateOf(
            when (randomizeStartingImage) {
                true -> Random.nextInt(images.size)
                false -> 0
            }
        )
    }
    val backgroundImage = images[currentImage]
    val accentColor by animateColorAsState(
        targetValue = backgroundImage.accentColor,
        animationSpec = tween(
            durationMillis = transitionDuration.inWholeMilliseconds.toInt()
        ),
    )

    Box(modifier = Modifier.fillMaxSize()) {
        Crossfade(
            targetState = backgroundImage.image,
            animationSpec = tween(
                durationMillis = transitionDuration.inWholeMilliseconds.toInt()
            )
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
        content(accentColor)
    }

    LaunchedEffect(currentImage) {
        delay(transitionDelay)
        setCurrentImage((currentImage + 1) % images.size)
    }
}
