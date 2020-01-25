package de.darkatra.patcher.updater.service.model;

import lombok.Data;
import lombok.experimental.Accessors;

import java.nio.file.Paths;

@Data
@Accessors(chain = true)
public class ObsoleteFile implements ContextAware {
	private String dest;

	@Override
	public void applyContext(final Context context) {
		context.forEach((key, value) -> {
			dest = dest.replace(context.getPrefix() + key + context.getSuffix(), value);
		});
		dest = Paths.get(dest).normalize().toString();
	}
}
