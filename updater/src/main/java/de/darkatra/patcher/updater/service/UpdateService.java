package de.darkatra.patcher.updater.service;

import static de.darkatra.patcher.updater.properties.UpdaterProperties.UPDATER_TEMP_NAME;
import de.darkatra.patcher.updater.properties.UpdaterProperties;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.util.ProcessUtils;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.InitializingBean;
import org.springframework.boot.system.ApplicationHome;
import org.springframework.stereotype.Service;

import java.io.File;
import java.io.IOException;
import java.nio.file.Path;

@Slf4j
@Service
@RequiredArgsConstructor
public class UpdateService implements InitializingBean {

	public static final String UNINSTALL_CURRENT_PARAMETER = "--uninstall-current";
	public static final String INSTALL_PARAMETER = "--install";

	private final ApplicationHome currentJar = new ApplicationHome(UpdateService.class);
	private final Context context;
	private final DownloadService downloadService;
	private final HashingService hashingService;
	private final UpdaterProperties updaterProperties;
	private Path updaterTempLocation;

	@Override
	public void afterPropertiesSet() {
		final Path patcherUserDir = Path.of(context.get("patcherUserDir"));
		updaterTempLocation = patcherUserDir.resolve(UPDATER_TEMP_NAME);
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
}
