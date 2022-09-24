package de.darkatra.bfme2.util

import java.text.DecimalFormat
import kotlin.math.log10
import kotlin.math.pow

object StringUtils {

    fun humanReadableSize(size: Long): String {
        if (size <= 0) {
            return "0 B"
        }
        val units = arrayOf("B", "kB", "MB", "GB", "TB", "EB")
        val digitGroups = (log10(size.toDouble()) / log10(1024.0)).toInt()
        return DecimalFormat("#,##0.0").format(size / 1024.0.pow(digitGroups.toDouble())) + " " + units[digitGroups]
    }
}
