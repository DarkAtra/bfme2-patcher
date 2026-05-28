package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.assetdat.AssetDatFileReader
import de.darkatra.bfme2.assetdat.AssetDatFileWriter
import java.nio.file.Path
import kotlin.io.path.deleteIfExists
import kotlin.io.path.pathString

private const val ORIGINAL_ASSET_DAT = "bfme2_orig_asset.dat"
private const val MOD_ASSET_DAT = "mod_asset.dat"
private const val OUTPUT_FILE_NAME = "asset.dat"

fun main() {
    AssetBuilderApplication.build()
}

object AssetBuilderApplication {

    private val assetDatFileReader = AssetDatFileReader()
    private val assetDatFileWriter = AssetDatFileWriter()

    fun build() {

        println("Building asset.dat...")

        val originalAssetDatPath = Path.of(ORIGINAL_ASSET_DAT)
        println("Reading original asset.dat from: ${originalAssetDatPath.pathString}")
        val originalAssetDat = assetDatFileReader.read(originalAssetDatPath)

        val modAssetDatPath = Path.of(MOD_ASSET_DAT)
        println("Reading mod asset.dat from: ${modAssetDatPath.pathString}")
        val modAssetDat = assetDatFileReader.read(modAssetDatPath)

        val mergedAssetDatPath = Path.of(OUTPUT_FILE_NAME)
        println("Writing merged asset.dat to: ${mergedAssetDatPath.pathString}")

        mergedAssetDatPath.deleteIfExists()

        val mergedAssetDat = originalAssetDat.merge(modAssetDat)

        assetDatFileWriter.write(mergedAssetDatPath, mergedAssetDat)

        println("Success!")
    }
}
