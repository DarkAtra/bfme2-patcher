package de.darkatra.patcher.updater.service.model;

import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.experimental.Accessors;

import java.nio.file.Paths;

@Data
@NoArgsConstructor
@Accessors(chain = true)
public class LatestUpdater implements ContextAware {
	private String src;
	private String dest;
	private Long packetSize;
	private String checksum;

	@Override
	public void applyContext(final Context context) {
		context.forEach((key, value) -> {
			src = src.replace(context.getPrefix() + key + context.getSuffix(), value);
			dest = dest.replace(context.getPrefix() + key + context.getSuffix(), value);
		});
		dest = Paths.get(dest).normalize().toString();
	}
}
