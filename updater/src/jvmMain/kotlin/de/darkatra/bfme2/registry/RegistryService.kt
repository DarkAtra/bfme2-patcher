package de.darkatra.bfme2.registry

import com.sun.jna.platform.win32.Advapi32Util
import com.sun.jna.platform.win32.WinReg.HKEY_LOCAL_MACHINE
import java.nio.file.Path

object RegistryService {

    private const val BASE_GAME_REGISTRY_KEY = "SOFTWARE\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Battle for Middle-earth II"
    private const val EXPANSION_REGISTRY_KEY = "SOFTWARE\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king"

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
}
