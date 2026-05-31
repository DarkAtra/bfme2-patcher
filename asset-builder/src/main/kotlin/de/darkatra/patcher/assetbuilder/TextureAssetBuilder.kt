package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.assetdat.model.Asset
import de.darkatra.bfme2.assetdat.model.AssetEntry
import de.darkatra.bfme2.assetdat.model.AssetEntryKind
import java.nio.file.Path
import java.util.Locale
import kotlin.io.path.getLastModifiedTime
import kotlin.io.path.name

class TextureAssetBuilder : AssetBuilder {

    override fun build(assetFile: Path): Asset {

        val normalizedAssetName = "${assetFile.name.lowercase(Locale.ROOT).substringBeforeLast(".")}.tga"

        val assetEntry = AssetEntry(
            name = normalizedAssetName,
            kind = AssetEntryKind.TEXTURE,
            offset = 0u, // only present for w3d
            size = 0u, // only present for w3d
            dependencyNames = emptyList(), // only present for w3d
        )

        return Asset(
            name = normalizedAssetName,
            fileTime = assetFile.getLastModifiedTime().toInstant(),
            assetEntries = listOf(assetEntry)
        )
    }
}
