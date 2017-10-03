package de.darkatra.patcher.model;

import com.google.common.collect.Constraints;
import lombok.Data;

import java.util.ArrayList;
import java.util.List;

@Data
public class Patch {
	private final List<Packet> packets = Constraints.constrainedList(new ArrayList<>(), Constraints.notNull());
	private final Version version;
}