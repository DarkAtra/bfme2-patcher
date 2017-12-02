package de.darkatra.patcher.updatebuilder.gui.controller;

import com.google.gson.GsonBuilder;
import de.darkatra.patcher.model.Patch;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.TextArea;
import javafx.stage.Stage;

public class PrintedPatchWindowController {

    @FXML
    private Label patchTxtNameLabel;
    @FXML
    private TextArea printedPatchWindowTextArea;
    @FXML
    private Button confirmButton;


    private Stage stage;

    @FXML
    private void initialize(){
        confirmButton.setOnMouseClicked(event -> this.stage.close());
    }

    public void setStage(Stage stage) {
        this.stage = stage;
    }

    public void setPrintedPatchWindowArea(Patch patch) {
        this.printedPatchWindowTextArea.setText(new GsonBuilder().serializeNulls().setPrettyPrinting().create().toJson(patch));
    }
}
