package de.darkatra.bfme2.game

import org.assertj.core.api.Assertions.assertThat
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import java.nio.file.Path
import kotlin.io.path.readText

class OptionFileServiceTest {

    private val optionFileService = OptionFileService

    @Test
    fun `should generate default options file`(@TempDir tempDir: Path) {

        val tempFile = tempDir.resolve("options.ini")

        optionFileService.writeOptionsFile(tempFile, optionFileService.buildDefaultOptions())

        assertThat(tempFile.readText()).isEqualTo(
            """
            IdealStaticGameLOD = VeryLow
            VoiceVolume = 50.0
            Resolution = 1920 1080
            MovieVolume = 50.0
            SendDelay = no
            MusicVolume = 50.0
            AllHealthBars = yes
            AmbientVolume = 30.0
            HasSeenLogoMovies = yes
            FlashTutorial = 0
            ScrollFactor = 50
            AudioLOD = High
            TimesInGame = 1
            Brightness = 50.0
            SFXVolume = 50.0
            UseEAX3 = no
            StaticGameLOD = UltraHigh

            """.trimIndent()
        )
    }
}
