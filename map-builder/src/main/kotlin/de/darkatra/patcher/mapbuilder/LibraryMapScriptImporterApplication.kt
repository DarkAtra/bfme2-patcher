package de.darkatra.patcher.mapbuilder

import de.darkatra.bfme2.map.MapFileCompression
import de.darkatra.bfme2.map.librarymap.LibraryMaps
import de.darkatra.bfme2.map.librarymap.LibraryMapsList
import de.darkatra.bfme2.map.serialization.MapFileReader
import de.darkatra.bfme2.map.serialization.MapFileWriter
import java.nio.file.Path
import kotlin.io.path.createDirectories
import kotlin.io.path.deleteIfExists
import kotlin.io.path.name
import kotlin.io.path.pathString

private const val ORIGINAL_MAPS_DIR = "input"
private const val EDITED_MAPS_DIR = "output"

fun main() {
    LibraryMapScriptImporterApplication.import()
}

object LibraryMapScriptImporterApplication {

    private val mapFileReader = MapFileReader()
    private val mapFieWriter = MapFileWriter()

    fun import() {

        println("Importing library map scripts...")

        val originalMapsDir = Path.of(ORIGINAL_MAPS_DIR)
        println("Reading maps from: ${originalMapsDir.pathString}")

        val editedMapsDir = Path.of(EDITED_MAPS_DIR)
        println("Writing edited maps to: ${editedMapsDir.pathString}")

        println("* Editing Maps")
        originalMapsDir.readFilesInDirectory()
            .filter { file -> file.name.endsWith(".map") }
            .forEach { file ->

                val map = mapFileReader.read(file)
                if (map.libraryMapsList.libraryMaps.any { libraryMaps -> libraryMaps.names.contains("Libraries\\lib_gollumspawn\\lib_gollumspawn.map") }) {
                    println("** Map already contains the 'lib_gollumspawn' scripts. Skipping: ${originalMapsDir.relativize(file).pathString}")
                    return@forEach
                }

                println("** Importing 'lib_gollumspawn' scripts for map: ${originalMapsDir.relativize(file).pathString}")

                val indexOfCreepPlayer = map.sides.players
                    .map { player -> player.properties.find { property -> property.key.name == "playerName" } }
                    .indexOfFirst { property -> property?.value == "PlyrCreeps" }

                if (indexOfCreepPlayer < 0) {
                    println("** Map does not have a Player named 'PlyrCreeps'. Skipping: ${originalMapsDir.relativize(file).pathString}")
                    return@forEach
                }

                val editedMap = map.copy(
                    // import the 'lib_gollumspawn' scripts
                    libraryMapsList = LibraryMapsList(
                        map.libraryMapsList.libraryMaps.toMutableList().apply {
                            add(indexOfCreepPlayer, LibraryMaps(names = listOf("Libraries\\lib_gollumspawn\\lib_gollumspawn.map")))
                        }
                    )
                )

                val editedMapOutPath = editedMapsDir.resolve(originalMapsDir.relativize(file))
                editedMapOutPath.parent.createDirectories()
                editedMapOutPath.deleteIfExists()
                println("** Writing edited map to: ${editedMapOutPath.pathString}")
                mapFieWriter.write(editedMapOutPath, editedMap, MapFileCompression.ZLIB)
            }

        println("Success! Output directory: ${editedMapsDir.pathString}")
    }
}
