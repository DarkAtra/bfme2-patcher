package de.darkatra.patcher.exception;

public class ValidationException extends Exception {

	public ValidationException(final String message) {
		this(message, null);
	}

	public ValidationException(final Throwable cause) {
		this(null, cause);
	}

	public ValidationException(final String message, final Throwable cause) {
		super(message, cause);
	}
}
