package de.darkatra.bfme2.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Slider
import androidx.compose.material.Surface
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

internal data class ComponentContainerSize(
    val label: String,
    val width: Dp,
)

internal val componentContainerSizes = listOf(
    ComponentContainerSize("Small", 240.dp),
    ComponentContainerSize("Medium", 400.dp),
    ComponentContainerSize("Large", 640.dp),
)

internal val tint = Color(0xFF587FD0)

@Composable
fun UITestApplicationLayout() {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xffeeeeee))
            .verticalScroll(rememberScrollState())
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(24.dp),
    ) {
        Text(
            text = "Component layout tests",
            style = MaterialTheme.typography.h4,
        )

        DynamicSizeSection()
        ContentSizedContainerSection()
        GridSection()
        ContainerSizesSection()
    }
}

@Composable
private fun DynamicSizeSection() {
    var widthFraction by remember { mutableFloatStateOf(0.5f) }

    TestSection(
        title = "Dynamic growth and shrinkage",
        description = "Move the slider to resize both components inside the available width.",
    ) {
        BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
            val componentWidth = dynamicComponentWidth(
                fraction = widthFraction,
                minWidth = 180.dp,
                maxWidth = maxWidth,
            )

            Column {
                Text(text = "Width: ${componentWidth.value.roundToInt()} dp")
                Slider(
                    value = widthFraction,
                    onValueChange = { widthFraction = it },
                    modifier = Modifier.fillMaxWidth(),
                )
                ComponentSample(modifier = Modifier.width(componentWidth))
            }
        }
    }
}

@Composable
private fun ContentSizedContainerSection() {
    TestSection(
        title = "Content-sized container",
        description = "This container has no explicit width, so the button grows only as wide as its content.",
    ) {
        Surface(color = Color(0xffdddddd)) {
            NinePatchButton(
                onClick = {},
                tint = tint,
            ) {
                Text("Content-sized NinePatchButton")
            }
        }
    }
}

@Composable
private fun GridSection() {
    TestSection(
        title = "Grid cell stretching",
        description = "Each sample fills a cell in a fixed three-column grid.",
    ) {
        LazyVerticalGrid(
            columns = GridCells.Fixed(3),
            modifier = Modifier.fillMaxWidth().height(360.dp),
            contentPadding = PaddingValues(4.dp),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            items((1..6).toList()) { index ->
                ComponentContainer(
                    label = "Grid cell $index",
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
    }
}

@Composable
private fun ContainerSizesSection() {
    TestSection(
        title = "Multiple container sizes",
        description = "Compare the same components at small, medium, and large fixed widths.",
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            componentContainerSizes.forEach { size ->
                ComponentContainer(
                    label = "${size.label} (${size.width.value.roundToInt()} dp)",
                    modifier = Modifier.width(size.width),
                )
            }
        }
    }
}

@Composable
private fun TestSection(
    title: String,
    description: String,
    content: @Composable ColumnScope.() -> Unit,
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        elevation = 4.dp,
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.h6,
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = description,
                style = MaterialTheme.typography.body2,
            )
            content()
        }
    }
}

@Composable
private fun ComponentContainer(
    label: String,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier = modifier,
        color = Color(0xffdddddd),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(text = label, style = MaterialTheme.typography.caption)
            ComponentSample(modifier = Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun ComponentSample(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        NinePatchButton(
            onClick = {},
            tint = tint,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("NinePatchButton")
        }
        ProgressBar(
            progress = 0.65f,
            tint = tint,
            text = "ProgressBar 65%",
        )
    }
}

internal fun dynamicComponentWidth(
    fraction: Float,
    minWidth: Dp,
    maxWidth: Dp,
): Dp {
    val constrainedMinWidth = minOf(minWidth, maxWidth)
    val constrainedFraction = when {
        fraction.isFinite() -> fraction.coerceIn(0f, 1f)
        else -> 0f
    }
    return constrainedMinWidth + (maxWidth - constrainedMinWidth) * constrainedFraction
}
