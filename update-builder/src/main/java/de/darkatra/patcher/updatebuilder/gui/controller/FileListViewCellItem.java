package de.darkatra.patcher.updatebuilder.gui.controller;

import javafx.fxml.FXML;
import javafx.fxml.FXMLLoader;
import javafx.scene.layout.HBox;
import javafx.scene.text.Text;

import java.io.IOException;

public class FileListViewCellItem {

    @FXML
    private HBox hBox;
    @FXML
    private Text fileName;
    @FXML
    private Text filePath;

    public FileListViewCellItem() {

        FXMLLoader fxmlLoader = new FXMLLoader(getClass().getResource("/view/listCellItem.fxml"));
        fxmlLoader.setController(this);
        try {
            fxmlLoader.load();
        } catch (IOException e) {
            //TODO
            e.printStackTrace();
        }
    }

    public HBox gethBox() {
        return hBox;
    }

    public void setFileName(String fileName) {
        this.fileName.setText(fileName);
    }

    public void setFilePath(String filePath) {
        this.filePath.setText(filePath);
    }
}
