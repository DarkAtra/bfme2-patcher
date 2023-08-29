package de.darkatra.patcher.ifeo

import java.nio.file.Path
import kotlin.io.path.toPath

object ApplicationHome {
    private val applicationHome: Path = javaClass.protectionDomain.codeSource.location.toURI().toPath()

    val parent: Path
        get() = applicationHome.parent
}
