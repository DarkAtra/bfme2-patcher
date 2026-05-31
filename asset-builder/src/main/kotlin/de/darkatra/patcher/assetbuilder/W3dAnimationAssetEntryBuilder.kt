@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.assetdat.model.AssetEntry
import de.darkatra.bfme2.assetdat.model.AssetEntryKind
import de.darkatra.bfme2.w3d.model.W3dAnimationHeader
import de.darkatra.bfme2.w3d.model.W3dChunk
import de.darkatra.bfme2.w3d.model.W3dChunkType

class W3dAnimationAssetEntryBuilder : W3dAssetEntryBuilder {

    override fun build(w3dChunk: W3dChunk): AssetEntry {

        val animationHeaderChunk = w3dChunk.extractSubChunk(W3dChunkType.W3D_CHUNK_ANIMATION_HEADER)
        val animationHeader = animationHeaderChunk.payload as W3dAnimationHeader

        return AssetEntry(
            name = "A*${animationHeader.hierarchyName}.${animationHeader.name}",
            kind = AssetEntryKind.ANIMATION,
            offset = w3dChunk.start,
            size = w3dChunk.end - w3dChunk.start,
            dependencyNames = emptyList(),
        )
    }
}
