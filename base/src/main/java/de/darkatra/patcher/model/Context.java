package de.darkatra.patcher.model;

import lombok.ToString;

import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

@ToString
public class Context {
	private final Map<String, Object> map = new LinkedHashMap<>();

	public Context put(final String key, final Object value) {
		map.put(key, value);
		return this;
	}

	public Context putIfAbsent(final String key, final Object value) {
		map.putIfAbsent(key, value);
		return this;
	}

	public boolean containsKey(String key) {
		return map.containsKey(key);
	}

	public Set<Map.Entry<String, Object>> entrySet() {
		return map.entrySet();
	}

	public Set<String> keySet() {
		return map.keySet();
	}

	public Collection<Object> values() {
		return map.values();
	}

	public boolean isEmpty() {
		return map.isEmpty();
	}

	public <T> Optional<T> getValue(final String key, final Class<T> classOfT) throws ClassCastException {
		return Optional.ofNullable(classOfT.cast(map.get(key)));
	}

	public Optional<String> getString(final String key) throws ClassCastException {
		return getValue(key, String.class);
	}

	public Optional<Integer> getInteger(final String key) throws ClassCastException {
		return getValue(key, Integer.class);
	}

	public Optional<Double> getDouble(final String key) throws ClassCastException {
		return getValue(key, Double.class);
	}

	public Optional<Boolean> getBoolean(final String key) throws ClassCastException {
		return getValue(key, Boolean.class);
	}

	public void apply(final Context other) {
		other.map.forEach(this.map::put);
	}
}
