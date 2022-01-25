package de.darkatra.patcher.mapbuilder

import de.darkatra.bfme2.Vector3
import de.darkatra.bfme2.big.BigArchive
import de.darkatra.bfme2.big.BigArchiveVersion
import de.darkatra.bfme2.map.MapFile
import de.darkatra.bfme2.map.reader.MapFileReader
import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import java.time.Instant
import java.util.stream.Collectors
import java.util.zip.CRC32
import kotlin.io.path.fileSize
import kotlin.io.path.getLastModifiedTime
import kotlin.io.path.inputStream
import kotlin.io.path.name
import kotlin.io.path.pathString


const val MAP_DIR = "maps-camera-fix"
const val OUTPUT_FILE_NAME = "update-builder/rotwk/!map_test.big" // TODO: finalize file name

fun main() {
	MapBuilderApplication.build()
}

object MapBuilderApplication {

	private val ZERO = Instant.parse("1601-01-01T00:00:00Z")

	fun build() {

		println("Building map archive...")

		val inputDir = Path.of(MAP_DIR)
		println("Reading files from: ${inputDir.pathString}")

		val outFile = Path.of(OUTPUT_FILE_NAME)
		outFile.toFile().also {
			it.mkdirs()
			it.delete()
		}

		val mapCache = mutableListOf<MapCacheEntry>()

		println("* Map archive: ${outFile.pathString}")
		val bigArchive = BigArchive(BigArchiveVersion.BIG_F, outFile)
		readFilesInDirectory(inputDir)
			.filter { file -> !file.name.endsWith(".map.uncompressed") }
			.forEach { file ->
				println("** Adding file to archive: ${inputDir.relativize(file).pathString}")
				bigArchive.createEntry(Path.of("maps").resolve(inputDir.relativize(file)).toString()).outputStream().use {
					it.write(Files.readAllBytes(file))
				}

				if (file.name.endsWith(".map")) {
					println("** Adding map cache entry for: ${inputDir.relativize(file).pathString}")
					val map = MapFileReader().read(file)
					mapCache.add(MapCacheEntry(
						mapPath = Path.of("maps").resolve(inputDir.relativize(file)).toString(),
						fileSize = file.fileSize(),
						fileCRC = generateCRC(file),
						timestampLo = winFileTimeFromInstant(file.getLastModifiedTime().toInstant()).toInt(),
						timestampHi = (winFileTimeFromInstant(file.getLastModifiedTime().toInstant()) shr 32).toInt(),
						isOfficial = true,
						isMultiplayer = (map.multiplayerPositions?.size ?: 1) > 1,
						isScenarioMP = map.worldSettings.find { it.key.name == "isScenarioMultiplayer" }?.value as Boolean? ?: false,
						// not sure why, but the game multiplies the coordinates by factor 10
						extentMin = Vector3(map.heightMap.borders[0].x1.toFloat() * 10f, map.heightMap.borders[0].y1.toFloat() * 10f, 0f),
						extentMax = Vector3(map.heightMap.borders[0].x2.toFloat() * 10f, map.heightMap.borders[0].y2.toFloat() * 10f, 0f),
						player1Start = getWaypointForPlayer(1, map),
						player2Start = getWaypointForPlayer(2, map),
						player3Start = getWaypointForPlayer(3, map),
						player4Start = getWaypointForPlayer(4, map),
						player5Start = getWaypointForPlayer(5, map),
						player6Start = getWaypointForPlayer(6, map),
						player7Start = getWaypointForPlayer(7, map),
						player8Start = getWaypointForPlayer(8, map),
						initialCameraPosition = map.objects.filter { it.typeName == "*Waypoints/Waypoint" }
							.find { it.properties.find { prop -> prop.key.name == "waypointName" }?.value == "InitialCameraPosition" }
							?.position
					))
				}
			}
		println("** Generating mapcache.ini...")

		bigArchive.createEntry("maps\\mapcache.ini").outputStream().use {
			mapCache.forEach { entry ->
				entry.serialize(it)
				it.write(System.lineSeparator().toByteArray())
				it.write(System.lineSeparator().toByteArray())
			}
		}
		bigArchive.writeToDisk()

		println("** Completed Map archive!")

		println("Success! Output: ${outFile.pathString}")
	}

	// max file depth is 2
	private fun readFilesInDirectory(directory: Path): Set<Path> {
		return Files.walk(directory, 2).use { stream ->
			stream
				.filter { path: Path -> Files.isRegularFile(path) }
				.collect(Collectors.toSet())
		}
	}

	// TODO: this doesn't generate the same CRC as the game
	private fun generateCRC(file: Path): Long {
		val crc = CRC32()
		file.inputStream().buffered().use {
			crc.update(it.readBytes())
		}
		return crc.value
	}

	private fun getWaypointForPlayer(player: Int, map: MapFile): Vector3? {
		return map.objects.filter { it.typeName == "*Waypoints/Waypoint" }
			.find { it.properties.find { prop -> prop.key.name == "waypointName" }?.value == "Player_${player}_Start" }
			?.position
	}

	private fun winFileTimeFromInstant(instant: Instant): Long {
		val duration: Duration = Duration.between(ZERO, instant)
		return duration.seconds * 10000000 + duration.nano / 100
	}
}

