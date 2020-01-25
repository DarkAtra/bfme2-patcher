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
	requires com.sun.jna.platform;
	requires org.bouncycastle.provider;
	requires commons.io;

	exports de.darkatra.patcher.updatebuilder;
	exports de.darkatra.patcher.updatebuilder.config to spring.beans, spring.context;
	exports de.darkatra.patcher.updatebuilder.service.model;
	exports de.darkatra.patcher.updatebuilder.properties to spring.beans, spring.boot;
	exports de.darkatra.patcher.updatebuilder.gui.controller to spring.beans;
	exports de.darkatra.patcher.updatebuilder.service to spring.beans;

	opens de.darkatra.patcher.updatebuilder to spring.core;
	opens de.darkatra.patcher.updatebuilder.config to spring.core;
	opens de.darkatra.patcher.updatebuilder.properties to org.hibernate.validator, spring.core;
	opens de.darkatra.patcher.updatebuilder.gui.controller to javafx.fxml;
	opens de.darkatra.patcher.updatebuilder.service to spring.core;
}
