package de.darkatra.patcher.updatebuilder.gui.controller;

import com.google.gson.GsonBuilder;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.Version;
import de.darkatra.patcher.service.HashingService;
import de.darkatra.patcher.updatebuilder.gui.GUIApplication;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.scene.Node;
import javafx.scene.control.Alert;
import javafx.scene.control.Button;
import javafx.scene.control.ListView;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.TextArea;
import javafx.scene.input.Dragboard;
import javafx.scene.input.TransferMode;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.Priority;
import javafx.scene.text.Text;
import javafx.stage.FileChooser;
import lombok.extern.slf4j.Slf4j;
import org.bouncycastle.jcajce.provider.digest.SHA3;
import org.jetbrains.annotations.NotNull;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.stereotype.Service;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.util.Base64;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.stream.Collectors;


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

    private GUIApplication guiApplication;

    private HashingService hashingService;


    public void setApplicationContext(ConfigurableApplicationContext applicationContext){
        hashingService = applicationContext.getBean(HashingService.class);
    }


    public void initialize() {

        addFilesButton.setOnMouseClicked(event -> {
            final FileChooser fc = new FileChooser();
            fc.setInitialDirectory(new File("."));
            fc.setTitle("Select a File");
            final List<File> files = fc.showOpenMultipleDialog(guiApplication.getStage());
            if (files != null) {
                listView.getItems().addAll(files);
            }
        });

        removeFilesButton.setOnMouseClicked(event -> {
            List<File> toRemove = listView.getSelectionModel().getSelectedItems();
            if (toRemove != null) {
                listView.getItems().removeAll(toRemove);
                listView.getSelectionModel().clearSelection();
            }
        });

        createPatchlistButton.setOnMouseClicked(event -> {


        });

        printPatchlistButton.setOnMouseClicked(event -> buildDialog().showAndWait());

        listView.getSelectionModel().setSelectionMode(SelectionMode.MULTIPLE);
        listView.setItems(FXCollections.observableArrayList());
        listView.setOnDragOver(event -> {
            Dragboard dragboard = event.getDragboard();
            if (dragboard.hasFiles()) {
                event.acceptTransferModes(TransferMode.LINK);
            }
        });
        listView.setOnDragDropped(event -> {
            Dragboard db = event.getDragboard();
            if (db.hasFiles()) {
                List<File> files = db.getFiles();
                for (File file : files) {
                    listView.getItems().add(file);
                }
            }
            event.setDropCompleted(true);
            event.consume();
        });


    }

    private Patch buildPatchFromList() {
        Path currentDir = new File(".").toPath().normalize().toAbsolutePath();
        Patch p = new Patch(new Version(0, 0, 1));
        p.getPackets().addAll(listView.getItems().stream().map(file -> {
            try {
                final Optional<String> sha3Checksum = hashingService.getSHA3Checksum(file);
                if (sha3Checksum.isPresent()) {
                    final Path relativePathToCWD = currentDir.relativize(file.toPath()).normalize();
                    return new Packet("${serverUrl}/" + relativePathToCWD.toString(), "${flyffDir}/" + relativePathToCWD.toFile().getPath(), file.length(), LocalDateTime.now(), sha3Checksum.get(), false);
                }
            } catch (IOException | InterruptedException e) {
                e.printStackTrace();
            }
            return null;
        }).filter(Objects::nonNull).collect(Collectors.toList()));
        return p;
    }

    private Alert buildDialog() {
        Alert alert = new Alert(Alert.AlertType.INFORMATION);
        alert.setTitle("Patchlist Printer");
        alert.setHeaderText("version.txt");
        alert.setContentText("Could not find file blabla.txt!");
        alert.setResizable(true);
        final Patch patch = buildPatchFromList();
        TextArea textArea = new TextArea(new GsonBuilder().serializeNulls().setPrettyPrinting().create().toJson(patch));
        textArea.setEditable(false);
        textArea.setWrapText(true);
        textArea.setMaxWidth(Double.MAX_VALUE);
        textArea.setMaxHeight(Double.MAX_VALUE);
        GridPane.setVgrow(textArea, Priority.ALWAYS);
        GridPane.setHgrow(textArea, Priority.ALWAYS);
        alert.getDialogPane().setContent(textArea);
        return alert;
    }


    public void setGuiApplication(GUIApplication guiApplication) {
        this.guiApplication = guiApplication;
    }
}
