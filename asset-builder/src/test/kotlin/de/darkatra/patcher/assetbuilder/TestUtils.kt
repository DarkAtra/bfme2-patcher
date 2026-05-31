package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.assetdat.model.Asset
import java.io.InputStream
import java.nio.file.Path
import java.util.function.BiPredicate
import kotlin.io.path.ExperimentalPathApi
import kotlin.io.path.copyToRecursively
import kotlin.io.path.toPath

object TestUtils {

    fun getInputStream(name: String): InputStream {
        return TestUtils::class.java.getResource(name)!!.openStream().buffered()
    }

    @OptIn(ExperimentalPathApi::class)
    fun copyFolderFromResources(name: String, target: Path) {
        TestUtils::class.java.getResource(name)!!.toURI().toPath().copyToRecursively(target, overwrite = true, followLinks = false)
    }

    fun timestampIgnoringAssetDatFileEquals(): BiPredicate<Asset, Asset> {
        return { a, b ->
            a.name == b.name && a.assetEntries.size == b.assetEntries.size && a.assetEntries == b.assetEntries
        }
    }
}
