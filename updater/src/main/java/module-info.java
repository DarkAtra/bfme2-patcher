module de.darkatra.patcher.updater {
	requires static lombok;

	requires javafx.controls;
	requires javafx.fxml;
	requires spring.beans;
	requires spring.core;
	requires spring.context;
	requires spring.web;
	requires spring.boot;
	requires spring.boot.autoconfigure;
	requires java.validation;
	requires com.fasterxml.jackson.databind;
	requires com.fasterxml.classmate;
	requires org.slf4j;

	exports de.darkatra.patcher.updater;
	exports de.darkatra.patcher.updater.gui.element to spring.beans, spring.context, javafx.fxml;
	exports de.darkatra.patcher.updater.properties to spring.beans, spring.boot;
	exports de.darkatra.patcher.updater.gui.controller to spring.beans;
	exports de.darkatra.patcher.updater.service to spring.beans;

	opens de.darkatra.patcher.updater to spring.core;
	opens de.darkatra.patcher.updater.properties to org.hibernate.validator, spring.core;
	opens de.darkatra.patcher.updater.gui.controller to javafx.fxml;
	opens de.darkatra.patcher.updater.gui.element to spring.core;
	opens de.darkatra.patcher.updater.service to spring.core;
}
