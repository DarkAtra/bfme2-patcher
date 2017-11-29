package de.darkatra.patcher.updatebuilder.gui.controller;

import com.google.gson.GsonBuilder;
import de.darkatra.patcher.config.ContextConfig;
import de.darkatra.patcher.model.Packet;
import de.darkatra.patcher.model.Patch;
import de.darkatra.patcher.model.Version;
import de.darkatra.patcher.service.HashingService;
import de.darkatra.patcher.updatebuilder.gui.GUIApplication;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.scene.Node;
import javafx.scene.control.*;
import javafx.scene.input.Dragboard;
import javafx.scene.input.TransferMode;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.Priority;
import javafx.scene.text.Text;
import javafx.stage.DirectoryChooser;
import javafx.stage.FileChooser;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.io.filefilter.TrueFileFilter;
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
import java.util.*;
import java.util.stream.Collectors;


public class MainWindowController {
    @FXML
    private Button addFilesButton, addDirectoryButton, removeFilesButton, createPatchlistButton, printPatchlistButton, confirmVersionButton, cancelVersionButton;
    @FXML
    private ListView<File> listView;
    @FXML
    private TextField versionTextField;
    @FXML
    private GridPane versionTextFieldButtons;
    @FXML
    private Label invalidInputLabel;

    private Version version;

    private GUIApplication guiApplication;

    private HashingService hashingService;
    private ContextConfig contextConfig;


    public void setApplicationContext(ConfigurableApplicationContext applicationContext) {
        this.hashingService = applicationContext.getBean(HashingService.class);
        this.contextConfig = applicationContext.getBean(ContextConfig.class);
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

        addDirectoryButton.setOnMouseClicked(event -> {
            final DirectoryChooser dc = new DirectoryChooser();
            dc.setInitialDirectory(new File((".")));
            dc.setTitle("Select a directory");
            final File directory = dc.showDialog(guiApplication.getStage());
            if (directory != null) {
                final Collection<File> files = FileUtils.listFiles(directory, null, true);
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

        confirmVersionButton.setOnMouseClicked(event -> {

            String versionText = versionTextField.getText();
            if (validateVersionData(versionText)) {
                String[] versionData = versionText.split("\\.");
                int major = Integer.parseInt(versionData[0]);
                int minor = Integer.parseInt(versionData[1]);
                int build = Integer.parseInt(versionData[2]);
                this.version = new Version(major, minor, build);
                hideTextFieldButtons();
                hideInvalidLabel();
            } else {
                invalidInputLabel.setVisible(true);
            }
        });

        cancelVersionButton.setOnMouseClicked(event -> {
            hideTextFieldButtons();
            hideInvalidLabel();
            versionTextField.clear();
        });

        hideTextFieldButtons();
        versionTextFieldButtons.visibleProperty().bind(versionTextFieldButtons.managedProperty());

    }

    private boolean validateVersionData(String version) {
        String[] data = version.split("\\.");

        if (data.length != 3) {
            return false;
        }
        for (String isDigit :
                data) {
            char[] characters = isDigit.toCharArray();
            for (char character : characters) {
                if (!(48 <= character && character <= 57)) {
                    return false;
                }
            }
        }
        return true;
    }

    private void hideTextFieldButtons() {
        versionTextFieldButtons.setManaged(false);
    }

    private void hideInvalidLabel() {
        invalidInputLabel.setVisible(false);
    }

    @FXML
    private void onTextFieldClicked() {
        hideInvalidLabel();
        versionTextFieldButtons.setManaged(true);
    }

    private Patch buildPatchFromList() {
        final Path currentDir = new File(".").toPath().normalize().toAbsolutePath();
        final Patch p = new Patch(new Version(0, 0, 1));
        p.getPackets().addAll(listView.getItems().stream().map(file -> {
            try {
                final Optional<String> sha3Checksum = hashingService.getSHA3Checksum(file);
                if (sha3Checksum.isPresent()) {
                    final Path relativePathToCWD = currentDir.relativize(file.toPath()).normalize();
                    // nunr dateien im arbeitsverzeichnis
                    //Pfad im applicationcontext nachschauen des dst
                    //configurationconfig
                    //ersetze c:user/../desktop mit %(desktop) aus app config
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
