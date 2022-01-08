package de.darkatra.patcher.modbuilder

import com.sun.jna.platform.win32.Advapi32Util
import com.sun.jna.platform.win32.WinReg
import java.nio.file.Path
import java.nio.file.Paths
import java.util.Optional

const val BFME2_REGISTRY_KEY = "SOFTWARE\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Battle for Middle-earth II"
const val BFME2EP1_REGISTRY_KEY = "SOFTWARE\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king"

object BfmeLocationService {

	/**
	 * Resolves the BfME 2 install dir by reading the windows registry.
	 *
	 * @return the BfME 2 install dir path
	 */
	fun findBfME2HomeDirectory(): Optional<Path> {
		return try {
			Optional.of(Paths.get(Advapi32Util.registryGetStringValue(WinReg.HKEY_LOCAL_MACHINE, BFME2_REGISTRY_KEY, "InstallPath")).normalize())
		} catch (e: Exception) {
			Optional.empty()
		}
	}

	/**
	 * Resolves the BfME 2 user dir by reading the windows registry.
	 *
	 * @return the BfME 2 user dir path
	 */
	fun findBfME2UserDirectory(): Optional<Path> {
		return try {
			Optional
				.of(Paths.get(System.getenv("APPDATA"), Advapi32Util.registryGetStringValue(WinReg.HKEY_LOCAL_MACHINE, BFME2_REGISTRY_KEY, "UserDataLeafName")))
		} catch (e: Exception) {
			Optional.empty()
		}
	}

	/**
	 * Resolves the BfME 2 EP 1 install dir by reading the windows registry.
	 *
	 * @return the BfME 2 EP 1 install dir path
	 */
	fun findBfME2RotWKHomeDirectory(): Optional<Path> {
		return try {
			Optional.of(Paths.get(Advapi32Util.registryGetStringValue(WinReg.HKEY_LOCAL_MACHINE, BFME2EP1_REGISTRY_KEY, "InstallPath")))
		} catch (e: Exception) {
			Optional.empty()
		}
	}

	/**
	 * Resolves the BfME 2 EP 1 user dir by reading the windows registry.
	 *
	 * @return the BfME 2 EP 1 user dir path
	 */
	fun findBfME2RotWKUserDirectory(): Optional<Path> {
		return try {
			Optional
				.of(Paths.get(System.getenv("APPDATA"), Advapi32Util.registryGetStringValue(WinReg.HKEY_LOCAL_MACHINE, BFME2EP1_REGISTRY_KEY, "UserDataLeafName")))
		} catch (e: Exception) {
			Optional.empty()
		}
	}
}
