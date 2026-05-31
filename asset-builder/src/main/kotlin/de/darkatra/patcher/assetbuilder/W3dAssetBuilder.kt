@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.assetdat.model.Asset
import de.darkatra.bfme2.assetdat.model.AssetEntry
import de.darkatra.bfme2.w3d.W3dFileReader
import de.darkatra.bfme2.w3d.model.W3dChunkType
import java.nio.file.Path
import java.util.Locale
import kotlin.io.path.getLastModifiedTime
import kotlin.io.path.name

class W3dAssetBuilder : AssetBuilder {

    private val w3dFileReader = W3dFileReader()

    private val chunkTypeToW3dAssetEntryBuilder = mapOf(
        W3dChunkType.W3D_CHUNK_ANIMATION to W3dAnimationAssetEntryBuilder(),
        W3dChunkType.W3D_CHUNK_BOX to W3dBoxAssetEntryBuilder(),
        W3dChunkType.W3D_CHUNK_COMPRESSED_ANIMATION to W3dCompressedAnimationAssetEntryBuilder(),
        W3dChunkType.W3D_CHUNK_HIERARCHY to W3dHierarchyAssetEntryBuilder(),
        W3dChunkType.W3D_CHUNK_HLOD to W3dHLodAssetEntryBuilder(),
        W3dChunkType.W3D_CHUNK_MESH to W3dMeshAssetEntryBuilder(),
    )

    override fun build(assetFile: Path): Asset {

        val assetEntries = mutableListOf<AssetEntry>()
        val w3dFile = w3dFileReader.read(assetFile)

        for (w3dChunk in w3dFile.chunks) {

            val builder = chunkTypeToW3dAssetEntryBuilder[w3dChunk.type]
                ?: continue

            assetEntries.add(builder.build(w3dChunk))
        }

        return Asset(
            name = assetFile.name.lowercase(Locale.ROOT),
            fileTime = assetFile.getLastModifiedTime().toInstant(),
            assetEntries = assetEntries
        )
    }
}
