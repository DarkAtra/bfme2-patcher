package de.darkatra.patcher.model;

import lombok.Data;

import java.time.LocalDateTime;

@Data
public class Packet {
	private final String src;
	private final String dest;
	private final Long packetSize;
	private final LocalDateTime dateTime;
	private final String checksum;
	private final boolean backupExisting;
}