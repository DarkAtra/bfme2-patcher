package de.darkatra.patcher.updatebuilder.gui.controller;

import de.darkatra.patcher.updatebuilder.gui.GUIApplication;
import javafx.beans.value.ChangeListener;
import javafx.beans.value.ObservableValue;
import javafx.collections.FXCollections;
import javafx.collections.ListChangeListener;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.scene.control.Button;
import javafx.scene.control.ListCell;
import javafx.scene.control.ListView;
import javafx.scene.control.SelectionMode;
import javafx.scene.input.Dragboard;
import javafx.scene.input.TransferMode;
import javafx.util.Callback;

import java.io.File;
import java.util.List;

public class MainWindowController {

    @FXML
    private Button addFilesButton;
    @FXML
    private Button removeFilesButton;
    @FXML
    private Button createPatchlistButton;
    @FXML
    private Button printPatchlistButton;
    @FXML
    private ListView<File> listView;


    private ObservableList<File> observableList;
    private GUIApplication guiApplication;


    public void initialize() {

        initializeListView();

        addFilesButton.setOnMouseClicked(event -> {
            System.out.println("addfiles");
        });

        removeFilesButton.setOnMouseClicked(event -> {
            System.out.println("removeFiles");
            List<File> toRemove = listView.getSelectionModel().getSelectedItems();
            observableList.removeAll(toRemove);
            listView.getSelectionModel().clearSelection();
            listView.setItems(observableList);
        });

        createPatchlistButton.setOnMouseClicked(event -> {
            System.out.println("createPatchlist");
        });

        printPatchlistButton.setOnMouseClicked(event -> {
            System.out.println("printpatchlist");
        });

        listView.setOnDragOver(event -> {
            Dragboard dragboard = event.getDragboard();
            if (dragboard.hasFiles()) {
                event.acceptTransferModes(TransferMode.LINK);
            }

        });

        listView.setOnDragDropped(event -> {
            Dragboard db = event.getDragboard();
//            System.out.print("ondrag");
            if (db.hasFiles()) {
                List<File> files = db.getFiles();
                for (File file :
                        files) {
                    System.out.println(file.getName());
//                observableList.add(file.getAbsolutePath());

                    listView.getItems().add(file);
                }
            }

            event.setDropCompleted(true);
            event.consume();
        });

//        listView.setItems(observableList);
    }

    private void initializeListView() {
        observableList = FXCollections.observableArrayList();
//        observableList.addAll("a", "b", "c", "d");
        listView.getSelectionModel().setSelectionMode(SelectionMode.MULTIPLE);

        listView.setItems(observableList);
        listView.setCellFactory(new Callback<ListView<File>, ListCell<File>>() {
            @Override
            public ListCell<File> call(ListView<File> listView) {
                return new FileListViewCell();
            }
        });
    }

    public void setGuiApplication(GUIApplication guiApplication) {
        this.guiApplication = guiApplication;
    }


}
