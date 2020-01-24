package de.darkatra.patcher.updatebuilder.model;

import lombok.Data;
import lombok.experimental.Accessors;

import java.time.Instant;

@Data
@Accessors(chain = true)
public class Packet {
	private String src;
	private String dest;
	private Long packetSize;
	private Instant dateTime;
	private String checksum;
	private boolean backupExisting;
	private Compression compression;
}
