package de.darkatra.patcher.updater.service.model;

import lombok.Getter;

import java.util.Hashtable;

@Getter
public class Context extends Hashtable<String, String> {
	private final String prefix = "${";
	private final String suffix = "}";
}
