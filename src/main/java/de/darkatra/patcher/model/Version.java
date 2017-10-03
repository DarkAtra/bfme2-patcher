package de.darkatra.patcher.model;

import lombok.Data;

@Data
public class Version {
	private final int majorVersion;
	private final int minorVersion;
	private final int buildVersion;

	@Override
	public String toString() {
		return String.format("%d.%02d.%03d", majorVersion, minorVersion, buildVersion);
	}
}