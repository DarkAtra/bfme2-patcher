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
     * B:\Electronic Arts\Aufstieg des Hexenkönigs\
     */
    ROTWK_DIR_NAME("rotwk", "\${rotwkHomeDir}"),
    ROTWK_PATCH_202_DIR_NAME("patch-202", "\${rotwkHomeDir}"),

    /**
     * B:\Electronic Arts\Aufstieg des Hexenkönigs\.patcher\
     */
    PATCH_DIR_NAME("patcher", "\${patcherUserDir}");
}
