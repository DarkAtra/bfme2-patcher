package de.darkatra.patcher.updater.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import de.darkatra.patcher.updater.service.model.Context;
import de.darkatra.patcher.updater.service.model.PatcherState;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.InitializingBean;
import org.springframework.stereotype.Service;

import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Path;

@Slf4j
@Service
@RequiredArgsConstructor
public class PatcherStateService implements InitializingBean {

	private static final String PATCHER_STATE_FILE_NAME = "patcher-state.json";

	private final Context context;
	private final ObjectMapper objectMapper;

	private Path patcherUserDir;

	@Override
	public void afterPropertiesSet() {
		patcherUserDir = Path.of(context.get("patcherUserDir"));
	}

	public PatcherState loadPatcherState() {
		try {
			return objectMapper.readValue(patcherUserDir.resolve(PATCHER_STATE_FILE_NAME).normalize().toFile(), PatcherState.class);
		} catch (final IOException e) {
			log.error("Could not parse patcher state.", e);
			// fallback to default patcher state if unable to read
			return new PatcherState();
		}
	}

	public void persistPatcherState(final PatcherState patcherState) throws IOException {
		if (!patcherUserDir.toFile().exists()) {
			patcherUserDir.toFile().mkdirs();
		}
		objectMapper.writeValue(new FileOutputStream(patcherUserDir.resolve(PATCHER_STATE_FILE_NAME).normalize().toFile()), patcherState);
	}
}
