package de.darkatra.patcher.model.communication;

import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@NoArgsConstructor
@AllArgsConstructor
public class RequiresUpdateDto {
	private boolean requiresUpdate = false;
}
