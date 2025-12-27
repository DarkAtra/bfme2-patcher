package de.darkatra.bfme2.registry

import com.sun.jna.platform.win32.Advapi32
import com.sun.jna.platform.win32.Advapi32Util
import com.sun.jna.platform.win32.WinReg.HKEY
import com.sun.jna.platform.win32.WinReg.HKEYByReference
import com.sun.jna.platform.win32.WinReg.HKEY_CURRENT_USER
import com.sun.jna.platform.win32.WinReg.HKEY_LOCAL_MACHINE
import com.sun.jna.ptr.IntByReference
import de.darkatra.bfme2.UpdaterContext
import io.goodforgod.graalvm.hint.annotation.DynamicProxyHint
import io.goodforgod.graalvm.hint.annotation.ReflectionHint
import java.nio.file.Path
import kotlin.io.path.absolutePathString

@ReflectionHint(types = [
    HKEY::class,
    HKEYByReference::class,
    IntByReference::class,
])
@DynamicProxyHint(
    DynamicProxyHint.Configuration(interfaces = [Advapi32::class])
)
object RegistryService {

    private const val BASE_GAME_REGISTRY_KEY = "SOFTWARE\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Battle for Middle-earth II"
    private const val EXPANSION_REGISTRY_KEY = "SOFTWARE\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king"
    private const val COMPAT_REGISTRY_KEY = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers"

    /**
     * The registry key that allows setting a 'Debugger' for the expansion.
     * If a 'Debugger' is set, opening the original lotrbfme2ep1.exe will launch whatever is set as value.
     * For example:
     * 'Debugger' to 'C:\MyApp\app.exe'
     * would launch 'C:\MyApp\app.exe' instead of the original game.
     *
     * Elevated permissions are required to write the registry key.
     */
    private const val HOOK_REGISTRY_KEY = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\lotrbfme2ep1.exe"

    fun findBaseGameHomeDirectory(): Path {
        return Path.of(
            Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, BASE_GAME_REGISTRY_KEY, "InstallPath")
        ).normalize()
    }

    fun findBaseGameUserDirectory(): Path {
        return Path.of(
            System.getenv("APPDATA"),
            Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, BASE_GAME_REGISTRY_KEY, "UserDataLeafName")
        ).normalize()
    }

    fun findExpansionHomeDirectory(): Path {
        return Path.of(
            Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, EXPANSION_REGISTRY_KEY, "InstallPath")
        ).normalize()
    }

    fun findExpansionUserDirectory(): Path {
        return Path.of(
            System.getenv("APPDATA"),
            Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, EXPANSION_REGISTRY_KEY, "UserDataLeafName")
        ).normalize()
    }

    fun hasExpansionDebugger(): Boolean {
        if (!Advapi32Util.registryValueExists(HKEY_LOCAL_MACHINE, HOOK_REGISTRY_KEY, "Debugger")) {
            return false
        }
        return Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, HOOK_REGISTRY_KEY, "Debugger") != ""
    }

    fun setExpansionDebugger(path: Path) {

        if (!Advapi32Util.registryValueExists(HKEY_LOCAL_MACHINE, HOOK_REGISTRY_KEY, "Debugger")) {
            Advapi32Util.registryCreateKey(HKEY_LOCAL_MACHINE, HOOK_REGISTRY_KEY)
        }

        Advapi32Util.registrySetStringValue(HKEY_LOCAL_MACHINE, HOOK_REGISTRY_KEY, "Debugger", path.absolutePathString())
    }

    fun resetExpansionDebugger() {
        Advapi32Util.registrySetStringValue(HKEY_LOCAL_MACHINE, HOOK_REGISTRY_KEY, "Debugger", "")
    }

    fun getExpansionVersion(): Int {
        return Advapi32Util.registryGetIntValue(HKEY_LOCAL_MACHINE, EXPANSION_REGISTRY_KEY, "Version")
    }

    fun updateExpansionVersion(version: Int) {

        if (!Advapi32Util.registryValueExists(HKEY_LOCAL_MACHINE, EXPANSION_REGISTRY_KEY, "Version")) {
            Advapi32Util.registryCreateKey(HKEY_LOCAL_MACHINE, EXPANSION_REGISTRY_KEY)
        }

        Advapi32Util.registrySetIntValue(HKEY_LOCAL_MACHINE, EXPANSION_REGISTRY_KEY, "Version", version)
    }

    fun getExpansionCompatMode(): String? {
        val appPath = findExpansionHomeDirectory().resolve(UpdaterContext.ROTWK_EXE_NAME).absolutePathString()
        if (!Advapi32Util.registryValueExists(HKEY_CURRENT_USER, COMPAT_REGISTRY_KEY, appPath)) {
            return null
        }
        return Advapi32Util.registryGetStringValue(HKEY_CURRENT_USER, COMPAT_REGISTRY_KEY, appPath)
    }

    fun updateExpansionCompatMode(compatMode: String) {

        val appPath = findExpansionHomeDirectory().resolve(UpdaterContext.ROTWK_EXE_NAME).absolutePathString()
        if (!Advapi32Util.registryValueExists(HKEY_CURRENT_USER, COMPAT_REGISTRY_KEY, appPath)) {
            Advapi32Util.registryCreateKey(HKEY_CURRENT_USER, COMPAT_REGISTRY_KEY)
        }

        Advapi32Util.registrySetStringValue(HKEY_CURRENT_USER, COMPAT_REGISTRY_KEY, appPath, compatMode)
    }
}
