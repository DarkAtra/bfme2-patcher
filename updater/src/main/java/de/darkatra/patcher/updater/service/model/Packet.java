package de.darkatra.patcher.updater.service.model;

import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import de.darkatra.patcher.updater.deserializer.LegacyInstantConverter;
import lombok.Data;
import lombok.experimental.Accessors;

import java.time.Instant;

@Data
@Accessors(chain = true)
public class Packet {
	private String src;
	private String dest;
	private Long packetSize;
	@JsonDeserialize(converter = LegacyInstantConverter.class)
	private Instant dateTime;
	private String checksum;
	private boolean backupExisting;
}
