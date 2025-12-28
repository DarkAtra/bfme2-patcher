package de.darkatra.patcher.mapbuilder

import java.nio.file.Files
import java.nio.file.Path
import java.util.stream.Collectors
import kotlin.io.path.isDirectory

fun Path.readFilesInDirectory(maxDepth: Int = 2): Set<Path> {

    if (!this.isDirectory()) {
        throw IllegalStateException("readFilesInDirectory only works for directories.")
    }

    return Files.walk(this, maxDepth).use { stream ->
        stream
            .filter { path: Path -> Files.isRegularFile(path) }
            .collect(Collectors.toSet())
    }
}
