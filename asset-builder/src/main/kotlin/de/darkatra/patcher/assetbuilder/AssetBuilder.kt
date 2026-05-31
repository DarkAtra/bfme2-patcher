package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.assetdat.model.Asset
import java.nio.file.Path

interface AssetBuilder {

    fun build(assetFile: Path): Asset
}
