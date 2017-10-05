package de.darkatra.patcher.model;

import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

public class Context {
	private final Map<String, Object> map;

	public Context() {
		map = new LinkedHashMap<>();
	}

	public Object put(String key, Object value) {
		return map.put(key, value);
	}

	public Object putIfAbsent(String key, Object value) {
		return map.putIfAbsent(key, value);
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

	public <T> Optional<T> getValue(String key, Class<T> classOfT) throws ClassCastException {
		return Optional.ofNullable(classOfT.cast(map.get(key)));
	}

	public Optional<Object> getValue(String key) throws ClassCastException {
		return getValue(key, Object.class);
	}

	public Optional<String> getString(String key) throws ClassCastException {
		return getValue(key, String.class);
	}

	public Optional<Integer> getInteger(String key) throws ClassCastException {
		return getValue(key, Integer.class);
	}

	public Optional<Double> getDouble(String key) throws ClassCastException {
		return getValue(key, Double.class);
	}

	public Optional<Boolean> getBoolean(String key) throws ClassCastException {
		return getValue(key, Boolean.class);
	}

	@Override
	public String toString() {
		return "Context{" +
				"map=" + map +
				'}';
	}

	public void apply(Context other) {
		other.map.forEach(this.map::put);
	}
}