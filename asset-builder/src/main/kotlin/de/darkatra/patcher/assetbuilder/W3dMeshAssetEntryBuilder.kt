@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.assetdat.model.AssetEntry
import de.darkatra.bfme2.assetdat.model.AssetEntryKind
import de.darkatra.bfme2.w3d.model.W3dChunk
import de.darkatra.bfme2.w3d.model.W3dChunkType
import de.darkatra.bfme2.w3d.model.W3dMeshHeader
import de.darkatra.bfme2.w3d.model.W3dSubChunks
import de.darkatra.bfme2.w3d.model.W3dTextureName
import java.util.Locale

class W3dMeshAssetEntryBuilder : W3dAssetEntryBuilder {

    override fun build(w3dChunk: W3dChunk): AssetEntry {

        val meshHeaderChunk = w3dChunk.extractSubChunk(W3dChunkType.W3D_CHUNK_MESH_HEADER3)
        val meshHeader = meshHeaderChunk.payload as W3dMeshHeader

        val meshTexturesChunk = w3dChunk.extractOptionalSubChunk(W3dChunkType.W3D_CHUNK_TEXTURES)
        val meshTextures: List<W3dTextureName> = ((meshTexturesChunk?.payload as W3dSubChunks?)?.children ?: emptyList()).asSequence()
            .map { it.payload }
            .filterIsInstance<W3dSubChunks>()
            .flatMap { it.children }
            .filter { it.type == W3dChunkType.W3D_CHUNK_TEXTURE_NAME }
            .map { it.payload as W3dTextureName }
            .toList()

        return AssetEntry(
            name = "${meshHeader.containerName}.${meshHeader.meshName}",
            kind = AssetEntryKind.MESH,
            offset = w3dChunk.start,
            size = w3dChunk.end - w3dChunk.start,
            dependencyNames = meshTextures.map { it.value.lowercase(Locale.ROOT) }.sorted(),
        )
    }
}
