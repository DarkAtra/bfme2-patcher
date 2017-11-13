package de.darkatra.patcher.updatebuilder.gui.controller;

import javafx.scene.control.ListCell;
import java.io.File;

public class FileListViewCell extends ListCell<File> {

    @Override
    public void updateItem(File item, boolean empty){

        if(empty||item==null){
//            setText(null);
            setGraphic(null);
        }else{
            System.out.println(item);
            FileListViewCellItem fileListViewCellItem = new FileListViewCellItem();
            fileListViewCellItem.setFileName(item.getName());
            fileListViewCellItem.setFilePath(item.getAbsolutePath());
            setGraphic(fileListViewCellItem.gethBox());
        }

    }

}
