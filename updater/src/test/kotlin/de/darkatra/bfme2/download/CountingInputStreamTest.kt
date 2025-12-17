package de.darkatra.bfme2.download

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test
import java.io.ByteArrayInputStream

class CountingInputStreamTest {

    @Test
    fun `should count single byte read`() {

        val countingInputStream = CountingInputStream(ByteArrayInputStream(ByteArray(1)))

        assertEquals(0, countingInputStream.count)
        assertEquals(0, countingInputStream.read())
        assertEquals(1, countingInputStream.count)

        countingInputStream.resetCount()

        assertEquals(0, countingInputStream.count)
    }

    @Test
    fun `should count bytes read`() {

        val countingInputStream = CountingInputStream(ByteArrayInputStream(ByteArray(10)))

        assertEquals(0, countingInputStream.count)
        assertEquals(10, countingInputStream.read(ByteArray(10)))
        assertEquals(10, countingInputStream.count)
    }

    @Test
    fun `should count byte range read`() {

        val countingInputStream = CountingInputStream(ByteArrayInputStream(ByteArray(5)))

        assertEquals(0, countingInputStream.count)
        assertEquals(3, countingInputStream.read(ByteArray(5), 1, 3))
        assertEquals(3, countingInputStream.count)
    }

    @Test
    fun `should count bytes read until EOF`() {

        val countingInputStream = CountingInputStream(ByteArrayInputStream(ByteArray(5)))

        assertEquals(0, countingInputStream.count)
        assertEquals(5, countingInputStream.read(ByteArray(10)))
        assertEquals(5, countingInputStream.count)

        assertEquals(-1, countingInputStream.read(ByteArray(1)))
        assertEquals(5, countingInputStream.count)
    }

    @Test
    fun `should count bytes skipped until EOF`() {

        val countingInputStream = CountingInputStream(ByteArrayInputStream(ByteArray(5)))

        assertEquals(0, countingInputStream.count)
        assertEquals(5, countingInputStream.skip(10))
        assertEquals(5, countingInputStream.count)
    }

    @Test
    fun `should count bytes until EOF`() {

        val countingInputStream = CountingInputStream(ByteArrayInputStream(ByteArray(5)))

        assertEquals(0, countingInputStream.count)
        assertEquals(5, countingInputStream.read(ByteArray(10)))
        assertEquals(5, countingInputStream.count)

        assertEquals(-1, countingInputStream.read(ByteArray(1)))
        assertEquals(5, countingInputStream.count)
    }
}
