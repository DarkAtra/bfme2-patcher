package de.darkatra.bfme2.util

import de.darkatra.bfme2.LOGGER
import de.darkatra.injector.logging.LogLevel
import de.darkatra.injector.logging.Logger
import java.util.logging.Level

object JavaLogger : Logger {

    override fun log(level: LogLevel, message: String, throwable: Throwable?) {
        LOGGER.log(
            when (level) {
                LogLevel.TRACE -> Level.FINE
                LogLevel.INFO -> Level.INFO
                LogLevel.WARN -> Level.WARNING
            },
            message,
            throwable
        )
    }
}
