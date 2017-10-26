package de.darkatra.patcher.updatebuilder.gui;

import javafx.application.Application;
import javafx.stage.Stage;

public class GUIApplication extends Application {
	@Override
	public void start(Stage primaryStage) throws Exception {
		System.out.println("Hallo");
	}

	public static void main(String[] args) {
		Application.launch(GUIApplication.class);
	}
}