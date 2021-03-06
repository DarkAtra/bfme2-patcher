package de.darkatra.patcher.updater.service.model;

import lombok.Data;
import lombok.experimental.Accessors;

import java.nio.file.Paths;
import java.time.Instant;

@Data
@Accessors(chain = true)
public class Packet implements ContextAware {
	private String src;
	private String dest;
	private Long packetSize;
	private Instant dateTime;
	private String checksum;
	private boolean backupExisting;
	private Compression compression;

	@Override
	public void applyContext(final Context context) {
		context.forEach((key, value) -> {
			src = src.replace(context.getPrefix() + key + context.getSuffix(), value);
			dest = dest.replace(context.getPrefix() + key + context.getSuffix(), value);
		});
		dest = Paths.get(dest).normalize().toString();
	}
}
