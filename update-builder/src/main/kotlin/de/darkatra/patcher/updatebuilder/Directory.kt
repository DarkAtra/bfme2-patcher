package de.darkatra.patcher.updatebuilder

enum class Directory(
    val dirName: String,
    val contextVariable: String
) {
    /**
     * For example: C:\Users\<User>\AppData\Roaming\Meine Der Herr der Ringe™, Aufstieg des Hexenkönigs™-Dateien\
    </User> */
    APPDATA_DIR_NAME("appdata", "\${rotwkUserDir}"),

    /**
     * B:\Electronic Arts\Die Schlacht um Mittelerde II\
     */
    BMFE2_DIR_NAME("bfme2", "\${bfme2HomeDir}"),

    /**
     * Same as ROTWK_DIR_NAME. Intended to be used for files that don't really belong to the mod itself (e.g. patch 2.01 and 2.02).
     */
    ROTWK_BASELINE_DIR_NAME("rotwk-baseline/patches", "\${rotwkHomeDir}"),

    /**
     * B:\Electronic Arts\Aufstieg des Hexenkönigs\
     */
    ROTWK_DIR_NAME("rotwk", "\${rotwkHomeDir}"),

    /**
     * B:\Electronic Arts\Aufstieg des Hexenkönigs\.patcher\
     */
    PATCH_DIR_NAME("patcher", "\${patcherUserDir}");
}
