package de.darkatra.patcher.model;

import lombok.Data;

@Data
public class Version {
	private final int majorVersion;
	private final int minorVersion;
	private final int buildVersion;

	public Version(int majorVersion, int minorVersion, int buildVersion) {
		this.majorVersion = majorVersion;
		this.minorVersion = minorVersion;
		this.buildVersion = buildVersion;
	}

	public Version(Version other) {
		this.majorVersion = other.majorVersion;
		this.minorVersion = other.minorVersion;
		this.buildVersion = other.buildVersion;
	}

	@Override
	public String toString() {
		return String.format("%d.%02d.%03d", majorVersion, minorVersion, buildVersion);
	}
}