package de.darkatra.patcher.mapbuilder

import de.darkatra.bfme2.Vector3
import de.darkatra.bfme2.big.BigArchive
import de.darkatra.bfme2.big.BigArchiveVersion
import de.darkatra.bfme2.map.MapFile
import de.darkatra.bfme2.map.MapFileCompression
import de.darkatra.bfme2.map.serialization.MapFileReader
import de.darkatra.bfme2.map.serialization.MapFileWriter
import de.darkatra.bfme2.map.worldinfo.WorldInfo
import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import java.time.Instant
import kotlin.io.path.createDirectories
import kotlin.io.path.deleteIfExists
import kotlin.io.path.fileSize
import kotlin.io.path.getLastModifiedTime
import kotlin.io.path.inputStream
import kotlin.io.path.name
import kotlin.io.path.pathString

private const val ORIGINAL_MAPS_DIR = "original-maps"
private const val EDITED_MAPS_DIR = "edited-maps"
private const val OUTPUT_FILE_NAME = "!maps.big"

fun main(args: Array<String>) {
    MapBuilderApplication.build("skipEditingMaps" in args)
}

object MapBuilderApplication {

    private val ZERO = Instant.parse("1601-01-01T00:00:00Z")
    private val mapFileReader = MapFileReader()
    private val mapFieWriter = MapFileWriter()

    fun build(skipEditingMaps: Boolean) {

        println("Building map archive...")

        val originalMapsDir = Path.of(ORIGINAL_MAPS_DIR)
        println("Reading maps from: ${originalMapsDir.pathString}")

        val editedMapsDir = Path.of(EDITED_MAPS_DIR)
        println("Writing edited maps to: ${editedMapsDir.pathString}")

        println("* Editing Maps (skip=${skipEditingMaps})")
        if (!skipEditingMaps) {
            originalMapsDir.readFilesInDirectory()
                .filter { file -> file.name.endsWith(".map") }
                .forEach { file ->
                    println("** Editing map: ${originalMapsDir.relativize(file).pathString}")

                    val map = mapFileReader.read(file)
                    val editedMap = map.copy(
                        // set the max camera height to 640
                        worldInfo = WorldInfo(
                            map.worldInfo.properties
                                .filter { property -> property.key.name != "cameraMaxHeight" }
                                .toMutableList()
                                .apply {
                                    add(map.worldInfo["cameraMaxHeight"]!!.copy(value = 640f))
                                }
                        )
                    )

                    val editedMapOutPath = editedMapsDir.resolve(originalMapsDir.relativize(file))
                    editedMapOutPath.parent.createDirectories()
                    editedMapOutPath.deleteIfExists()
                    println("** Writing edited map to: ${editedMapOutPath.pathString}")
                    mapFieWriter.write(editedMapOutPath, editedMap, MapFileCompression.ZLIB)
                }
        }

        val outFile = Path.of(OUTPUT_FILE_NAME)
        outFile.toFile().also {
            it.mkdirs()
            it.delete()
        }

        val mapCache = mutableListOf<MapCacheEntry>()

        println("* Map archive: ${outFile.pathString}")
        val bigArchive = BigArchive(BigArchiveVersion.BIG_F, outFile)
        editedMapsDir.readFilesInDirectory()
            .filter { file -> !file.name.endsWith(".map.uncompressed") }
            .forEach { file ->
                println("** Adding file to archive: ${editedMapsDir.relativize(file).pathString}")
                bigArchive.createEntry(Path.of("maps").resolve(editedMapsDir.relativize(file)).toString()).outputStream().use {
                    it.write(Files.readAllBytes(file))
                }

                if (file.name.endsWith(".map")) {
                    println("** Adding map cache entry for: ${editedMapsDir.relativize(file).pathString}")
                    val map = mapFileReader.read(file)
                    mapCache.add(
                        MapCacheEntry(
                            mapPath = Path.of("maps").resolve(editedMapsDir.relativize(file)).toString(),
                            fileSize = file.fileSize(),
                            fileCRC = calculateCRC(file),
                            timestampLo = winFileTimeFromInstant(file.getLastModifiedTime().toInstant()).toInt(),
                            timestampHi = (winFileTimeFromInstant(file.getLastModifiedTime().toInstant()) shr 32).toInt(),
                            isOfficial = true,
                            isScenarioMP = map.worldInfo["isScenarioMultiplayer"]?.value as Boolean? ?: false,
                            extentMin = Vector3(0f, 0f, 0f),
                            // not sure why, but the game multiplies the coordinates by factor 10
                            extentMax = Vector3(map.heightMap.borders[0].x.toFloat() * 10f, map.heightMap.borders[0].y.toFloat() * 10f, 0f),
                            player1Start = getWaypointForPlayer(1, map),
                            player2Start = getWaypointForPlayer(2, map),
                            player3Start = getWaypointForPlayer(3, map),
                            player4Start = getWaypointForPlayer(4, map),
                            player5Start = getWaypointForPlayer(5, map),
                            player6Start = getWaypointForPlayer(6, map),
                            player7Start = getWaypointForPlayer(7, map),
                            player8Start = getWaypointForPlayer(8, map),
                            initialCameraPosition = map.objects.objects.filter { it.typeName == "*Waypoints/Waypoint" }
                                .find { it.properties.find { prop -> prop.key.name == "waypointName" }?.value == "InitialCameraPosition" }
                                ?.position
                        )
                    )
                }
            }
        println("** Generating mapcache.ini...")

        bigArchive.createEntry("maps\\mapcache.ini").outputStream().use {
            mapCache.sortedWith(Comparator.comparing(MapCacheEntry::mapPath)).forEach { entry ->
                entry.serialize(it)
                it.write(System.lineSeparator().toByteArray())
                it.write(System.lineSeparator().toByteArray())
            }
        }
        bigArchive.writeToDisk()

        println("** Completed Map archive!")

        println("Success! Output: ${outFile.pathString}")
    }

    // implementation based on https://github.com/electronicarts/CnC_Generals_Zero_Hour/blob/0a05454d8574207440a5fb15241b98ad0b435590/Generals/Code/GameEngine/Source/Common/crc.cpp#L55
    private fun calculateCRC(file: Path): UInt {
        var crc = 0u
        file.inputStream().buffered().use { inputStream ->
            var byte: UByte
            while (inputStream.read().also { byte = it.toUByte() } != -1) {
                val highBit = when {
                    crc and 0x80000000u != 0x0u -> 1u
                    else -> 0u
                }
                crc = crc shl 1
                crc += byte
                crc += highBit
            }
        }
        return crc
    }

    private fun getWaypointForPlayer(player: Int, map: MapFile): Vector3? {
        return map.objects.objects.filter { it.typeName == "*Waypoints/Waypoint" }
            .find { it.properties.find { prop -> prop.key.name == "waypointName" }?.value == "Player_${player}_Start" }
            ?.position
    }

    private fun winFileTimeFromInstant(instant: Instant): Long {
        val duration: Duration = Duration.between(ZERO, instant)
        return duration.seconds * 10000000 + duration.nano / 100
    }
}
