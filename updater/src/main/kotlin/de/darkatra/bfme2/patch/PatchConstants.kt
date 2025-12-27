package de.darkatra.bfme2.patch

object PatchConstants {
    const val SERVER_URL = "https://darkatra.de"
    const val UPDATER_URL = "$SERVER_URL/bfmemod2/updater.exe"
    const val UPDATER_ICON_URL = "$SERVER_URL/bfmemod2/icon.ico"
    const val PATCH_LIST_URL = "$SERVER_URL/bfmemod2/version.json"
    const val REQUIREMENTS_URL = "$SERVER_URL/bfmemod2/requirements.json"

    const val UPDATER_NAME = "updater.exe"
    const val UPDATER_TEMP_NAME = "_updater.exe"
    const val UPDATER_OLD_NAME = "updater.old.exe"
    const val UPDATER_IFEO_NAME = "updater-ifeo.exe"

    const val FALLBACK_UPDATER_NAME = "updater.jar"
    const val FALLBACK_UPDATER_TEMP_NAME = "_updater.jar"
    const val FALLBACK_UPDATER_OLD_NAME = "updater.old.jar"
}
