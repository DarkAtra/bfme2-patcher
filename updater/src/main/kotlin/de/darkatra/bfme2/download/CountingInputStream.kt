package de.darkatra.bfme2.download

import java.io.FilterInputStream
import java.io.InputStream
import java.util.concurrent.atomic.AtomicLong

class CountingInputStream(
    inputStream: InputStream
) : FilterInputStream(inputStream) {

    val count
        get() = _count.get()

    private val _count: AtomicLong = AtomicLong(0)

    fun resetCount() {
        _count.set(0)
    }

    override fun read(): Int {
        val result = `in`.read()
        if (result != -1) {
            _count.incrementAndGet()
        }
        return result
    }

    override fun read(b: ByteArray, off: Int, len: Int): Int {
        val result = `in`.read(b, off, len)
        if (result != -1) {
            _count.addAndGet(result.toLong())
        }
        return result
    }

    override fun skip(n: Long): Long {
        val result = `in`.skip(n)
        _count.addAndGet(result)
        return result
    }
}
