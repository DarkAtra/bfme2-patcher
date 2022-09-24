package de.darkatra.patcher.mapbuilder

import de.darkatra.bfme2.Vector3
import java.io.OutputStream
import java.util.Locale
import kotlin.math.max

data class MapCacheEntry(
    val mapPath: String,
    val fileSize: Long,
    val fileCRC: Long,
    val timestampLo: Int,
    val timestampHi: Int,
    val isOfficial: Boolean,
    val isScenarioMP: Boolean,
    val extentMin: Vector3,
    val extentMax: Vector3,
    val player1Start: Vector3?,
    val player2Start: Vector3?,
    val player3Start: Vector3?,
    val player4Start: Vector3?,
    val player5Start: Vector3?,
    val player6Start: Vector3?,
    val player7Start: Vector3?,
    val player8Start: Vector3?,
    val initialCameraPosition: Vector3?,
    val numPlayers: Int = max(
        listOf(player1Start, player2Start, player3Start, player4Start, player5Start, player6Start, player7Start, player8Start)
            .takeWhile { it != null }
            .size,
        1
    ),
    val isMultiplayer: Boolean = numPlayers > 1
) {

    fun serialize(outputStream: OutputStream) {
        outputStream.write("MapCache ".toByteArray())
        outputStream.write(escapeString(mapPath.replace("/", "\\")).toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  fileSize = $fileSize".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  fileCRC = $fileCRC".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  timestampLo = $timestampLo".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  timestampHi = $timestampHi".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  isOfficial = ${formatBoolean(isOfficial)}".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  isMultiplayer = ${formatBoolean(isMultiplayer)}".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  isScenarioMP = ${formatBoolean(isScenarioMP)}".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  numPlayers = $numPlayers".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  extentMin = ${formatVector3(extentMin)}".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  extentMax = ${formatVector3(extentMax)}".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  displayName = ${generateMapName(mapPath)}".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())
        outputStream.write("  description = ${generateMapDescription(mapPath)}".toByteArray())
        outputStream.write(System.lineSeparator().toByteArray())

        if (initialCameraPosition != null) {
            outputStream.write("  InitialCameraPosition = ${formatVector3(initialCameraPosition)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }

        // player start positions
        if (player1Start != null) {
            outputStream.write("  Player_1_Start = ${formatVector3(player1Start)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }
        if (player2Start != null) {
            outputStream.write("  Player_2_Start = ${formatVector3(player2Start)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }
        if (player3Start != null) {
            outputStream.write("  Player_3_Start = ${formatVector3(player3Start)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }
        if (player4Start != null) {
            outputStream.write("  Player_4_Start = ${formatVector3(player4Start)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }
        if (player5Start != null) {
            outputStream.write("  Player_5_Start = ${formatVector3(player5Start)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }
        if (player6Start != null) {
            outputStream.write("  Player_6_Start = ${formatVector3(player6Start)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }
        if (player7Start != null) {
            outputStream.write("  Player_7_Start = ${formatVector3(player7Start)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }
        if (player8Start != null) {
            outputStream.write("  Player_8_Start = ${formatVector3(player8Start)}".toByteArray())
            outputStream.write(System.lineSeparator().toByteArray())
        }

        outputStream.write("END".toByteArray())
    }

    private fun formatVector3(vector3: Vector3): String {
        return "X:${String.format(Locale.ROOT, "%.2f", vector3.x)} Y:${String.format(Locale.ROOT, "%.2f", vector3.y)} Z:${
            String.format(
                Locale.ROOT,
                "%.2f",
                vector3.z
            )
        }"
    }

    private fun formatBoolean(boolean: Boolean): String {
        return when (boolean) {
            true -> "yes"
            false -> "no"
        }
    }

    private fun escapeString(toEscape: String): String {
        val stringBuilder = StringBuilder()
        for (char in toEscape) {
            when (char.code) {
                in 48..57 -> stringBuilder.append(char)
                in 65..90 -> stringBuilder.append(char)
                in 97..122 -> stringBuilder.append(char)
                else -> stringBuilder.append("_${String.format("%02X", char.code)}")
            }
        }
        return stringBuilder.toString()
    }

    private fun generateMapName(mapPath: String): String {
        return escapeString("\u0024Map:${getEncodedMapName(mapPath)}".map { "$it\u0000" }.joinToString(""))
    }

    private fun generateMapDescription(mapPath: String): String {
        return escapeString("Map:${getEncodedMapName(mapPath)}/Desc".map { "$it\u0000" }.joinToString(""))
    }

    private fun getEncodedMapName(mapPath: String): String {
        return mapPath.split("\\")[1].split(" ").joinToString("").filter {
            it.code in 48..57 || it.code in 65..90 || it.code in 97..122
        }
    }
}
