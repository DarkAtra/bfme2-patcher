package de.darkatra.patcher.updater.service.model;

import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class PatcherState {
	private boolean hdEditionEnabled = false;
}
