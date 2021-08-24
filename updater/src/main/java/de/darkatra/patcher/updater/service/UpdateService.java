package de.darkatra.patcher.updater.service;

import static de.darkatra.patcher.updater.UpdaterApplication.APPLICATION_TITLE;
import static de.darkatra.patcher.updater.properties.UpdaterProperties.UPDATER_NAME;
import static de.darkatra.patcher.updater.properties.UpdaterProperties.UPDATER_OLD_NAME;
import static de.darkatra.patcher.updater.properties.UpdaterProperties.UPDATER_TEMP_NAME;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.util.ProcessUtils;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import mslinks.ShellLink;
import org.springframework.beans.factory.InitializingBean;
import org.springframework.boot.system.ApplicationHome;
import org.springframework.core.env.Environment;
import org.springframework.core.env.Profiles;
import org.springframework.stereotype.Service;
import org.springframework.util.StreamUtils;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Path;

@Slf4j
@Service
@RequiredArgsConstructor
public class UpdateService implements InitializingBean {

	public static final int MAX_RENAME_ATTEMPTS = 7;
	public static final String UNINSTALL_CURRENT_PARAMETER = "--uninstall-current";
	public static final String INSTALL_PARAMETER = "--install";

	private final Environment environment;
	private final ApplicationHome currentJar = new ApplicationHome(UpdateService.class);
	private final Context context;
	private final DownloadService downloadService;
	private final HashingService hashingService;
	private final UpdaterProperties updaterProperties;
	private Path patcherUserDir;
	private Path updaterTempLocation;
	private Path currentUpdaterLocation;
	private Path oldUpdaterLocation;
	private Path linkLocation;
	private Path linkIconLocation;

	@Override
	public void afterPropertiesSet() {
		patcherUserDir = Path.of(context.get("patcherUserDir"));
		updaterTempLocation = patcherUserDir.resolve(UPDATER_TEMP_NAME);
		currentUpdaterLocation = patcherUserDir.resolve(UPDATER_NAME);
		oldUpdaterLocation = patcherUserDir.resolve(UPDATER_OLD_NAME);
		linkLocation = Path.of(System.getProperty("user.home"), "Desktop", APPLICATION_TITLE + ".lnk");
		linkIconLocation = patcherUserDir.resolve("icon.ico");
	}

	public boolean isInCorrectLocation() {
		return environment.acceptsProfiles(Profiles.of("dev")) || currentJar.getSource().toPath().startsWith(patcherUserDir);
	}

	public void moveToCorrectLocation() throws IOException, InterruptedException {
		if (!patcherUserDir.toFile().exists()) {
			if (!patcherUserDir.toFile().mkdirs()) {
				throw new IllegalStateException("Could not create patcherUserDir.");
			}
		}
		StreamUtils.copy(new FileInputStream(currentJar.getSource()), new FileOutputStream(currentUpdaterLocation.toFile()));
		if (!linkLocation.toFile().exists()) {
			downloadService.downloadFile(
				updaterProperties.getUpdaterIconUrl().toExternalForm(),
				linkIconLocation.toFile().getAbsolutePath(),
				false,
				null
			);
			ShellLink.createLink(currentUpdaterLocation.toFile().getAbsolutePath())
				.setWorkingDir(currentUpdaterLocation.toFile().getParent())
				.setIconLocation(linkIconLocation.toFile().getAbsolutePath())
				.saveTo(linkLocation.toFile().getAbsolutePath());
		}
		ProcessUtils.runJar(currentUpdaterLocation);
	}

	public void performUninstallation() throws IOException {
		deleteFile(oldUpdaterLocation.toFile());
		if (attemptRename(currentUpdaterLocation, oldUpdaterLocation)) {
			ProcessUtils.runJar(oldUpdaterLocation, INSTALL_PARAMETER);
		}
	}

	public void performInstallation() throws IOException {
		if (attemptRename(updaterTempLocation, currentUpdaterLocation)) {
			ProcessUtils.runJar(currentUpdaterLocation);
		}
	}

	public void performCleanup() {
		deleteFile(oldUpdaterLocation.toFile());
	}

	public boolean isNewVersionAvailable() {
		try {
			final String latestUpdaterChecksum = hashingService.getSHA3Checksum(updaterProperties.getUpdaterUrl().openStream());

			final String currentUpdaterChecksum = hashingService.getSHA3Checksum(currentJar.getSource())
				.orElseThrow(() -> new IllegalStateException("Could not calculate SHA3 checksum of the current updater."));

			return !currentUpdaterChecksum.equals(latestUpdaterChecksum);
		} catch (Exception e) {
			log.error("Could not determine if a newer version of the updater exists.", e);
			return false;
		}
	}

	public boolean downloadLatestUpdaterVersion() throws InterruptedException {

		final File targetLocation = updaterTempLocation.toFile();

		if (targetLocation.exists()) {
			if (!targetLocation.delete()) {
				return false;
			}
		}

		return downloadService.downloadFile(
			updaterProperties.getUpdaterUrl().toString(),
			targetLocation.getAbsolutePath(),
			false,
			null
		);
	}

	public void installUpdate() throws IOException {
		ProcessUtils.runJar(updaterTempLocation, UNINSTALL_CURRENT_PARAMETER);
	}

	private boolean attemptRename(final Path from, final Path to) {
		for (int i = 0; i < MAX_RENAME_ATTEMPTS; i++) {
			if (from.toFile().renameTo(to.toFile())) {
				return true;
			}
			try {
				Thread.sleep(100L * (i + 1));
			} catch (final InterruptedException e) {
				return false;
			}
		}
		return false;
	}

	private void deleteFile(final File fileToDelete) {
		if (fileToDelete.exists()) {
			if (!fileToDelete.delete()) {
				log.error("Could not delete file: " + fileToDelete.getAbsolutePath());
			}
		}
	}
}
