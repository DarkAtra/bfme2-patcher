package de.darkatra.patcher.updater.deserializer;

import com.fasterxml.jackson.databind.util.StdConverter;

import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.Map;

public class LegacyInstantConverter extends StdConverter<Map<String, Map<String, Integer>>, Instant> {

	@Override
	public Instant convert(final Map<String, Map<String, Integer>> legacyDateFormat) {

		final Map<String, Integer> date = legacyDateFormat.get("date");
		final int year = date.get("year");
		final int month = date.get("month");
		final int day = date.get("day");

		final Map<String, Integer> time = legacyDateFormat.get("time");
		final int hour = time.get("hour");
		final int minute = time.get("minute");
		final int second = time.get("second");
		final int nano = time.get("nano");

		return LocalDateTime.of(year, month, day, hour, minute)
			.withSecond(second)
			.withNano(nano)
			.toInstant(ZoneOffset.ofHours(0));
	}
}
