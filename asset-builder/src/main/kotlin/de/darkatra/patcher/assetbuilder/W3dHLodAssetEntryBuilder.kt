@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.assetdat.model.AssetEntry
import de.darkatra.bfme2.assetdat.model.AssetEntryKind
import de.darkatra.bfme2.w3d.model.W3dChunk
import de.darkatra.bfme2.w3d.model.W3dChunkType
import de.darkatra.bfme2.w3d.model.W3dHLodHeader
import de.darkatra.bfme2.w3d.model.W3dHLodSubObject
import de.darkatra.bfme2.w3d.model.W3dSubChunks
import java.util.Locale

class W3dHLodAssetEntryBuilder : W3dAssetEntryBuilder {

    override fun build(w3dChunk: W3dChunk): AssetEntry {

        val hLodHeaderChunk = w3dChunk.extractSubChunk(W3dChunkType.W3D_CHUNK_HLOD_HEADER)
        val hLodHeader = hLodHeaderChunk.payload as W3dHLodHeader

        val hLodArrayChunk = w3dChunk.extractOptionalSubChunk(W3dChunkType.W3D_CHUNK_HLOD_LOD_ARRAY)

        val dependencies = ((hLodArrayChunk?.payload as W3dSubChunks?)?.children ?: emptyList())
            .filter { it.type == W3dChunkType.W3D_CHUNK_HLOD_SUB_OBJECT }
            .map { (it.payload as W3dHLodSubObject).name }
            .map { it.lowercase(Locale.ROOT) }
            .sorted()

        val hierarchyDependency = "h*${hLodHeader.hierarchyName.lowercase(Locale.ROOT)}"

        return AssetEntry(
            name = hLodHeader.name,
            kind = AssetEntryKind.HLOD,
            offset = w3dChunk.start,
            size = w3dChunk.end - w3dChunk.start,
            dependencyNames = dependencies + hierarchyDependency,
        )
    }
}
