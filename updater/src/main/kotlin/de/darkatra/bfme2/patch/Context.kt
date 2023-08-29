package de.darkatra.bfme2.patch

import java.nio.file.Path
import kotlin.io.path.absolutePathString
import kotlin.io.path.exists

class Context : HashMap<String, String>() {

    companion object {
        const val PREFIX = "\${"
        const val SUFFIX = "}"

        const val SERVER_URL_IDENTIFIER = "serverUrl"
        const val BFME2_HOME_DIR_IDENTIFIER = "bfme2HomeDir"
        const val BFME2_USER_DIR_IDENTIFIER = "bfme2UserDir"
        const val ROTWK_HOME_DIR_IDENTIFIER = "rotwkHomeDir"
        const val ROTWK_USER_DIR_IDENTIFIER = "rotwkUserDir"
        const val PATCHER_USER_DIR_IDENTIFIER = "patcherUserDir"
    }

    fun isValid(): Boolean {
        return containsKey(SERVER_URL_IDENTIFIER) &&
            containsKey(BFME2_HOME_DIR_IDENTIFIER) &&
            containsKey(BFME2_USER_DIR_IDENTIFIER) &&
            containsKey(ROTWK_HOME_DIR_IDENTIFIER) &&
            containsKey(ROTWK_USER_DIR_IDENTIFIER) &&
            containsKey(PATCHER_USER_DIR_IDENTIFIER)
    }

    fun ensureRequiredDirectoriesExist() {
        val patcherUserDir = getPatcherUserDir()
        if (!patcherUserDir.exists()) {
            if (!patcherUserDir.toFile().mkdirs()) {
                throw IllegalStateException("Could not create: ${patcherUserDir.absolutePathString()}")
            }
        }
    }

    fun getBfme2UserDir(): Path {
        return Path.of(get(BFME2_USER_DIR_IDENTIFIER)!!)
    }

    fun getRotwkHomeDir(): Path {
        return Path.of(get(ROTWK_HOME_DIR_IDENTIFIER)!!)
    }

    fun getRotwkUserDir(): Path {
        return Path.of(get(ROTWK_USER_DIR_IDENTIFIER)!!)
    }

    fun getPatcherUserDir(): Path {
        return Path.of(get(PATCHER_USER_DIR_IDENTIFIER)!!)
    }
}
