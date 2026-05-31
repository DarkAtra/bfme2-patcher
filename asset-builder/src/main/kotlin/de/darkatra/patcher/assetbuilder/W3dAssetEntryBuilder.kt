@file:OptIn(ExperimentalApi::class)

package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.ExperimentalApi
import de.darkatra.bfme2.assetdat.model.AssetEntry
import de.darkatra.bfme2.w3d.model.W3dChunk

interface W3dAssetEntryBuilder {

    fun build(w3dChunk: W3dChunk): AssetEntry
}
