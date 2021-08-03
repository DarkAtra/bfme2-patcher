package de.darkatra.patcher.updater.service.model;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

@Getter
@RequiredArgsConstructor
public class UpdateProgress {
	private final long current;
	private final long total;
}
