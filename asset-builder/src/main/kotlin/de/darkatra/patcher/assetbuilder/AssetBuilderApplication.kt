@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.assetdat.AssetDatFileReader
import de.darkatra.bfme2.assetdat.AssetDatFileWriter
import de.darkatra.bfme2.assetdat.model.Asset
import de.darkatra.bfme2.assetdat.model.AssetDatFile
import java.nio.file.Path
import java.util.Locale
import kotlin.io.path.absolutePathString
import kotlin.io.path.deleteIfExists
import kotlin.io.path.exists
import kotlin.io.path.isDirectory
import kotlin.io.path.listDirectoryEntries
import kotlin.io.path.name
import kotlin.io.path.pathString

private const val ORIGINAL_ASSET_DAT = "bfme2_orig_asset.dat"
private const val MOD_ASSET_DAT = "mod_asset.dat"
private const val OUTPUT_FILE_NAME = "asset.dat"

private const val ASSETS_ROOT_FOLDER = "art"
private const val ASSETS_TEXTURE_FOLDER = "compiledtextures"
private const val ASSETS_MODEL_FOLDER = "w3d"

fun main() {
    AssetBuilderApplication().build(true)
}

class AssetBuilderApplication(
    workingDirectory: Path = Path.of("."),
) {

    private val assetDatFileReader = AssetDatFileReader()
    private val assetDatFileWriter = AssetDatFileWriter()

    private val assetsRootFolder = workingDirectory.resolve(ASSETS_ROOT_FOLDER)
    private val assetsTextureFolder = assetsRootFolder.resolve(ASSETS_TEXTURE_FOLDER)
    private val assetsModelFolder = assetsRootFolder.resolve(ASSETS_MODEL_FOLDER)

    private val originalAssetDatPath = workingDirectory.resolve(ORIGINAL_ASSET_DAT)
    private val modAssetDatPath = workingDirectory.resolve(MOD_ASSET_DAT)
    private val mergedAssetDatPath = workingDirectory.resolve(OUTPUT_FILE_NAME)

    private val textureAssetBuilder = TextureAssetBuilder()
    private val w3dAssetBuilder = W3dAssetBuilder()

    private val extensionToAssetBuilder: Map<String, AssetBuilder> = mapOf(
        "dds" to textureAssetBuilder,
        "tga" to textureAssetBuilder,
        "w3d" to w3dAssetBuilder,
    )

    fun build(extendOriginal: Boolean) {

        println("Building asset.dat...")

        validateFolderStructure()
        buildModAssetDat()

        if (extendOriginal) {
            mergeOriginalAndModAssetDat()
        }

        println("Success!")
    }

    private fun validateFolderStructure() {

        if (!assetsRootFolder.exists()) {
            throw IllegalStateException("${assetsRootFolder.absolutePathString()} does not exist.")
        }

        validateNestedFolderStructure(assetsTextureFolder, setOf(".dds", ".tga"))
        validateNestedFolderStructure(assetsModelFolder, setOf(".w3d"))
    }

    private fun validateNestedFolderStructure(assetFolder: Path, allowedSuffixes: Set<String>) {

        if (!assetFolder.exists()) {
            throw IllegalStateException("${assetFolder.absolutePathString()} does not exist.")
        }

        if (assetFolder.listDirectoryEntries().any { !it.isDirectory() || it.name.length != 2 }) {
            throw IllegalStateException("${assetFolder.absolutePathString()} must only contain folders with exactly 2 character names.")
        }

        if (assetFolder.readFilesInDirectory().any { file -> allowedSuffixes.none { suffix -> file.name.endsWith(suffix, true) } }) {
            throw IllegalStateException("Folders in ${assetFolder.absolutePathString()} must only contain files with the following suffixes: $allowedSuffixes.")
        }
    }

    private fun buildModAssetDat() {

        val assets = mutableListOf<Asset>()

        for (assetFile in assetsRootFolder.readFilesInDirectory(3)) {

            val assetFileExtension = assetFile.name.lowercase(Locale.ROOT).substringAfterLast(".")

            val assetBuilder = extensionToAssetBuilder[assetFileExtension]
                ?: continue

            assets.add(assetBuilder.build(assetFile))
        }

        modAssetDatPath.deleteIfExists()
        assetDatFileWriter.write(modAssetDatPath, AssetDatFile(assets))
    }

    private fun mergeOriginalAndModAssetDat() {

        println("Merging $ORIGINAL_ASSET_DAT and $MOD_ASSET_DAT...")

        println("Reading original asset.dat from: ${originalAssetDatPath.pathString}")
        val originalAssetDat = assetDatFileReader.read(originalAssetDatPath)

        println("Reading mod asset.dat from: ${modAssetDatPath.pathString}")
        val modAssetDat = assetDatFileReader.read(modAssetDatPath)

        println("Writing merged asset.dat to: ${mergedAssetDatPath.pathString}")

        mergedAssetDatPath.deleteIfExists()

        val mergedAssetDat = originalAssetDat.merge(modAssetDat)

        assetDatFileWriter.write(mergedAssetDatPath, mergedAssetDat)
    }
}
