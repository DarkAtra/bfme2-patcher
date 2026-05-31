@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.w3d.model.W3dChunk
import de.darkatra.bfme2.w3d.model.W3dChunkType
import de.darkatra.bfme2.w3d.model.W3dSubChunks

internal fun W3dChunk.extractSubChunk(w3dChunkType: W3dChunkType): W3dChunk {

    return extractOptionalSubChunk(w3dChunkType)
        ?: throw IllegalStateException("W3dChunk has no sub chunk of type $w3dChunkType")
}

internal fun W3dChunk.extractOptionalSubChunk(w3dChunkType: W3dChunkType): W3dChunk? {

    if (payload !is W3dSubChunks) {
        throw IllegalStateException("W3dChunk#payload is not of type 'W3dSubChunks'")
    }

    return (payload as W3dSubChunks).children
        .firstOrNull { it.type == w3dChunkType }
}
