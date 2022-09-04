package de.darkatra.bfme2.selfupdate

import java.nio.file.Path
import kotlin.io.path.isRegularFile

class ApplicationHome {

    val source: Path = Path.of(javaClass.protectionDomain.codeSource.location.path)

    fun isRunningAsJar(): Boolean {
        return source.isRegularFile()
    }
}
