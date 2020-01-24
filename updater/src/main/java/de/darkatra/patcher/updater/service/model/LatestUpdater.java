package de.darkatra.patcher.updater.service.model;

import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.experimental.Accessors;

@Data
@NoArgsConstructor
@Accessors(chain = true)
public class LatestUpdater {
	private String location;
	private Long packetSize;
	private String checksum;

	public LatestUpdater(final LatestUpdater other) {
		this.location = other.location;
		this.packetSize = other.packetSize;
		this.checksum = other.checksum;
	}
}
