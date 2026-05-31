@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.assetdat.model.AssetEntry
import de.darkatra.bfme2.assetdat.model.AssetEntryKind
import de.darkatra.bfme2.w3d.model.W3dChunk
import de.darkatra.bfme2.w3d.model.W3dChunkType
import de.darkatra.bfme2.w3d.model.W3dHierarchyHeader

class W3dHierarchyAssetEntryBuilder : W3dAssetEntryBuilder {

    override fun build(w3dChunk: W3dChunk): AssetEntry {

        val hierarchyHeaderChunk = w3dChunk.extractSubChunk(W3dChunkType.W3D_CHUNK_HIERARCHY_HEADER)
        val hierarchyHeader = hierarchyHeaderChunk.payload as W3dHierarchyHeader

        return AssetEntry(
            name = "H*${hierarchyHeader.name}",
            kind = AssetEntryKind.HIERARCHY,
            offset = w3dChunk.start,
            size = w3dChunk.end - w3dChunk.start,
            dependencyNames = emptyList(),
        )
    }
}
