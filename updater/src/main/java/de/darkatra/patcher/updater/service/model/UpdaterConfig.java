package de.darkatra.patcher.updater.service.model;

import lombok.Data;

import java.util.Set;

@Data
public class UpdaterConfig {
	private boolean modEnabled;
	private long delayBetweenBackgroundChanges;
	private final Set<Resolution> patcherResolutions;
	private final Set<Resolution> gameResolutions;
}
