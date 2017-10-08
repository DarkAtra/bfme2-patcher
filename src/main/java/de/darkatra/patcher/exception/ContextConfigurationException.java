package de.darkatra.patcher.exception;

public class ContextConfigurationException extends Exception {
	public ContextConfigurationException() {
	}

	public ContextConfigurationException(String message) {
		super(message);
	}

	public ContextConfigurationException(String message, Throwable cause) {
		super(message, cause);
	}

	public ContextConfigurationException(Throwable cause) {
		super(cause);
	}
}