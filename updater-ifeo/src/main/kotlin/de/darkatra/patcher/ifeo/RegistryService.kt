package de.darkatra.patcher.ifeo

import com.sun.jna.platform.win32.Advapi32Util
import com.sun.jna.platform.win32.WinReg.HKEY_LOCAL_MACHINE
import java.nio.file.Path
import kotlin.io.path.absolutePathString

object RegistryService {

    /**
     * The registry key that allows setting a 'Debugger' for the expansion.
     * If a 'Debugger' is set, opening the original lotrbfme2ep1.exe will launch whatever is set as value.
     * For example, setting:
     * 'Debugger' to 'C:\MyApp\app.exe'
     * would launch 'C:\MyApp\app.exe' instead of the original game.
     *
     * Elevated permissions are required to write the registry key.
     */
    private const val HOOK_REGISTRY_KEY = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\lotrbfme2ep1.exe"

    fun setExpansionDebugger(path: Path) {
        Advapi32Util.registrySetStringValue(HKEY_LOCAL_MACHINE, HOOK_REGISTRY_KEY, "Debugger", path.absolutePathString())
    }

    fun resetExpansionDebugger() {
        Advapi32Util.registrySetStringValue(HKEY_LOCAL_MACHINE, HOOK_REGISTRY_KEY, "Debugger", "")
    }
}
