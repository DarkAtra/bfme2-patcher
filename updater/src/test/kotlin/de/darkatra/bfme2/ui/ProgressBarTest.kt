package de.darkatra.bfme2.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.text.style.TextAlign
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ProgressBarTest {

    @Test
    fun `determinate progress is constrained to the visible track`() {
        assertEquals(0f, progressBarIndicatorFraction(-0.1f))
        assertEquals(0.5f, progressBarIndicatorFraction(0.5f))
        assertEquals(1f, progressBarIndicatorFraction(1.1f))
    }

    @Test
    fun `invalid determinate progress produces an empty indicator`() {
        assertEquals(0f, progressBarIndicatorFraction(Float.NaN))
        assertEquals(0f, progressBarIndicatorFraction(Float.NEGATIVE_INFINITY))
        assertEquals(0f, progressBarIndicatorFraction(Float.POSITIVE_INFINITY))
    }

    @Test
    fun `progress track obscures the background while retaining the tint hue`() {
        val tint = Color(0x332474b5)
        val trackColor = progressBarTrackColor(tint)

        assertEquals(1f, trackColor.alpha)
        assertTrue(trackColor.blue > trackColor.red)
        assertTrue(trackColor.luminance() < tint.luminance())
    }

    @Test
    fun `progress text uses the enabled button text color`() {
        val tint = Color(0xff2474b5)
        val textStyle = progressBarTextStyle(tint)
        val textColor = textStyle.color

        assertTrue(textColor.blue > textColor.red)
        assertTrue(textColor.luminance() > tint.luminance())
        assertEquals(1f, textColor.alpha)
        assertEquals(TextAlign.Center, textStyle.textAlign)
        assertEquals(textStyle.fontSize, textStyle.lineHeight)
    }
}