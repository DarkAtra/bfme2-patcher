package de.darkatra.patcher.model;

import lombok.Data;
import lombok.RequiredArgsConstructor;

@Data
@RequiredArgsConstructor
public class Version {
	private final int majorVersion;
	private final int minorVersion;
	private final int buildVersion;

	public Version(final Version other) {
		this.majorVersion = other.majorVersion;
		this.minorVersion = other.minorVersion;
		this.buildVersion = other.buildVersion;
	}

	@Override
	public String toString() {
		return String.format("%d.%02d.%03d", majorVersion, minorVersion, buildVersion);
	}
}
