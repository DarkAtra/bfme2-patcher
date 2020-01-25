# Patcher
Der Patcher besteht aus zwei Anwendungen:
- **Updater**: Der Updater hält alle Dateien für ein Programm auf dem aktuellen Stand. Er berechnet beim start alle Unterschiede zwischen dem aktuellen Programm und der neusten Version vom Server. Dazu wird zu jeder Datei ein SHA-3 Hash gebildet und mit dem des Servers verglichen. Im Anschluss werden alle neueren/beschädigten Dateien vom Server heruntergeladen und alte Dateien gelöscht.
- **Update-Builder**: Der Update-Builder ist eine Anwendung, die das Erstellen der `version.json` erleichtert. Der Updater benötigt diese Datei um Unterschiede zwischen Client und Server zu berechnen. In dieser Datei befinden sich die Checksummen, Größenangaben, Pfade und Zeitstempel der einzelnen Dateien.

## Build
Um diese Anwendung zu bauen muss zuerst das Projekt geklont werden. Dazu wird der folgende Befehl auf der Kommandozeile ausgeführt.
```
git clone https://git.darkatra.de/DarkAtra/Patcher.git
```
Um das Projekt nun zu bauen wird der folgende Befehl auf der Kommandozeile ausgeführt.
```
mvn clean package
```
Die Jar-Dateien befinden sich nun im target-Ordner der jeweiligen Anwendung. Bsp.: `updater/target/updater-0.0.1-SNAPSHOT.jar`

## Develop
Sollte das Projekt nicht nicht geklont sein muss der folgende Befehl ausgeführt werden.
```
git clone https://git.darkatra.de/DarkAtra/Patcher.git
```
Im Anschluss kann eine beliebige IDE (z.B. Eclipse, IntelliJ) gestartet und das Maven-Projekt importiert werden. Benutzen Sie dazu den Import-Dialog der IDE.
Hinweis: Dieses Projekt benötigt mindestens JDK 9, da es bereits das Modulsystem von Java verwendet (Jigsaw).