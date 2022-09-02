package de.darkatra.bfme2.game

import java.nio.file.Path
import java.util.Hashtable
import kotlin.io.path.outputStream

class OptionFileService {

    fun writeOptionsFile(path: Path, options: Hashtable<String, Any>) {

        path.outputStream().bufferedWriter().use { fw ->

            for ((key, value) in options) {
                if (value == true || value == false) {
                    fw.write("$key = ${if (value == true) "yes" else "no"}\n")
                } else {
                    fw.write("$key = $value\n")
                }
            }

            fw.flush()
        }
    }

    fun buildDefaultOptions(): Hashtable<String, Any> {
        return Hashtable<String, Any>().apply {
            this["AllHealthBars"] = true
            this["AmbientVolume"] = 30.0
            this["AudioLOD"] = "High"
            this["Brightness"] = 50.0
            this["FlashTutorial"] = 0
            this["HasSeenLogoMovies"] = true
            this["IdealStaticGameLOD"] = "VeryLow"
            this["MovieVolume"] = 50.0
            this["MusicVolume"] = 50.0
            this["Resolution"] = "1920 1080"
            this["SFXVolume"] = 50.0
            this["ScrollFactor"] = 50
            this["SendDelay"] = false
            this["StaticGameLOD"] = "UltraHigh"
            this["TimesInGame"] = 1
            this["UseEAX3"] = false
            this["VoiceVolume"] = 50.0
        }
    }
}
