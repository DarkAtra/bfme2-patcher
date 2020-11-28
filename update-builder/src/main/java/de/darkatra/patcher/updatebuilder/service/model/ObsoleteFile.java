package de.darkatra.patcher.updatebuilder.service.model;

import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class ObsoleteFile {
	private String dest;
}
