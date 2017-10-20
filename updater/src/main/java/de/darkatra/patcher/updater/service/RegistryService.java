package de.darkatra.patcher.updater.service;

import static com.sun.jna.platform.win32.WinReg.HKEY_LOCAL_MACHINE;
import com.sun.jna.platform.win32.Advapi32Util;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Optional;

@Slf4j
@Service
public class RegistryService {
	private final String BFME2_REGISTRY_KEY = "SOFTWARE\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Battle for Middle-earth II";
	private final String BFME2EP1_REGISTRY_KEY = "SOFTWARE\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king";

	/**
	 * Resolves the BfME 2 install dir by reading the windows registry.
	 *
	 * @return the BfME 2 install dir path
	 */
	public Optional<Path> findBfME2HomeDirectory() {
		try {
			return Optional.of(Paths.get(Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, BFME2_REGISTRY_KEY, "InstallPath")).normalize());
		} catch(Exception e) {
			log.warn("Can not find the BfME2 install dir.", e);
			return Optional.empty();
		}
	}

	/**
	 * Resolves the BfME 2 user dir by reading the windows registry.
	 *
	 * @return the BfME 2 user dir path
	 */
	public Optional<Path> findBfME2UserDirectory() {
		try {
			return Optional.of(Paths.get(System.getenv("APPDATA"), Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, BFME2_REGISTRY_KEY, "UserDataLeafName")));
		} catch(Exception e) {
			log.warn("Can not find the BfME2 user dir.", e);
			return Optional.empty();
		}
	}

	/**
	 * Resolves the BfME 2 EP 1 install dir by reading the windows registry.
	 *
	 * @return the BfME 2 EP 1 install dir path
	 */
	public Optional<Path> findBfME2RotWKHomeDirectory() {
		try {
			return Optional.of(Paths.get(Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, BFME2EP1_REGISTRY_KEY, "InstallPath")));
		} catch(Exception e) {
			log.warn("Can not find the BfME2EP1 install dir.", e);
			return Optional.empty();
		}
	}

	/**
	 * Resolves the BfME 2 EP 1 user dir by reading the windows registry.
	 *
	 * @return the BfME 2 EP 1 user dir path
	 */
	public Optional<Path> findBfME2RotWKUserDirectory() {
		try {
			return Optional.of(Paths.get(System.getenv("APPDATA"), Advapi32Util.registryGetStringValue(HKEY_LOCAL_MACHINE, BFME2EP1_REGISTRY_KEY, "UserDataLeafName")));
		} catch(Exception e) {
			log.warn("Can not find the BfME2EP1 user dir.", e);
			return Optional.empty();
		}
	}
}