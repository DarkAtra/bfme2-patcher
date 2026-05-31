package de.darkatra.patcher.assetbuilder

import de.darkatra.bfme2.assetdat.AssetDatFileReader
import de.darkatra.bfme2.assetdat.model.Asset
import org.assertj.core.api.Assertions.assertThat
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Path
import kotlin.io.path.createDirectories
import kotlin.io.path.inputStream

class AssetBuilderApplicationTest {

    private val assetDatFileReader = AssetDatFileReader()

    @Test
    fun `should create correct assetdat`(@TempDir workingDirectory: Path) {

        val expectedAssetDat = TestUtils.getInputStream("/assetdats/asset-aragorn.dat").use(assetDatFileReader::read)

        val artFolder = workingDirectory.resolve("art")
        artFolder.createDirectories()

        TestUtils.copyFolderFromResources("/art", artFolder)

        AssetBuilderApplication(workingDirectory).build(false)

        val actualAssetDat = workingDirectory.resolve("mod_asset.dat").inputStream().use(assetDatFileReader::read)

        assertThat(actualAssetDat)
            .usingRecursiveComparison()
            .withEqualsForType(TestUtils.timestampIgnoringAssetDatFileEquals(), Asset::class.java)
            .isEqualTo(expectedAssetDat)
    }
}
