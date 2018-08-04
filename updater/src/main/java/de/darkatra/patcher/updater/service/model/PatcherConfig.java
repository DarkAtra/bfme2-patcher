package de.darkatra.patcher.updater.service.model;

import lombok.AllArgsConstructor;
import lombok.Data;

import java.util.LinkedHashSet;
import java.util.Set;

@Data
@AllArgsConstructor
public class PatcherConfig {
	private boolean modEnabled;
	private long delayBetweenBackgroundChanges = 15000;
	private final Set<Resolution> patcherResolutions = new LinkedHashSet<>();
	private final Set<Resolution> gameResolutions = new LinkedHashSet<>();
}