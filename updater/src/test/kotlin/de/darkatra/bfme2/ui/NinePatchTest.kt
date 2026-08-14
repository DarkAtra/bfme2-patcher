package de.darkatra.bfme2.ui

import kotlin.test.Test
import kotlin.test.assertContentEquals

class NinePatchTest {

    @Test
    fun `source pixels are scaled to the display density`() {

        val coordinates = calculateNinePatchDestinationAxis(
            destinationSize = 640,
            startInset = 47,
            endInset = 47,
            pixelScale = 2f,
            density = 2f,
        )

        assertContentEquals(intArrayOf(0, 47, 593, 640), coordinates)
    }

    @Test
    fun `fixed segments shrink proportionally when destination is too small`() {

        val coordinates = calculateNinePatchDestinationAxis(
            destinationSize = 60,
            startInset = 45,
            endInset = 46,
            pixelScale = 2f,
            density = 2f,
        )

        assertContentEquals(intArrayOf(0, 30, 30, 60), coordinates)
    }
}
