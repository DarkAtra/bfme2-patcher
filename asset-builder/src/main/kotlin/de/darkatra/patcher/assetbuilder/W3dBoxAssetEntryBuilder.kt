@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.assetdat.model.AssetEntry
import de.darkatra.bfme2.assetdat.model.AssetEntryKind
import de.darkatra.bfme2.w3d.model.W3dBox
import de.darkatra.bfme2.w3d.model.W3dChunk

class W3dBoxAssetEntryBuilder : W3dAssetEntryBuilder {

    override fun build(w3dChunk: W3dChunk): AssetEntry {

        return AssetEntry(
            name = (w3dChunk.payload as W3dBox).name,
            kind = AssetEntryKind.BOX,
            offset = w3dChunk.start,
            size = w3dChunk.end - w3dChunk.start,
            dependencyNames = emptyList(),
        )
    }
}
