package de.darkatra.patcher.updater.service.model;

import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@NoArgsConstructor
public class Version {
	private int majorVersion;
	private int minorVersion;
	private int buildVersion;

	public Version(final Version other) {
		this.majorVersion = other.majorVersion;
		this.minorVersion = other.minorVersion;
		this.buildVersion = other.buildVersion;
	}
}
