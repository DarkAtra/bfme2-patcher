# Patcher
Der Patcher besteht aus drei Anwendungen:
- **Updater**: Der Updater hällt alle Dateien für ein Programm auf dem aktuellen Stand. Er berechnet beim start alle Unterschiede zwischen dem aktuellen Programm und der neusten Version vom Server. Dazu wird zu jeder Datei ein SHA-3 Hash gebildet und mit dem des Servers verglichen. Im Anschluss werden alle neueren Dateien vom Server heruntergeladen und alte Dateien gelöscht. Außerdem kann der Launcher mithilfe dieses Tools aktualisiert werden.
- **Launcher**: Der Launcher installiert den Updater und hällt ihn auf dem aktuellen Stand. Der Launcher dient außerdem zum Starten des Updaters.
- **Update-Builder**: Der Update-Builder ist eine Anwendung, die das Erstellen einer version.txt erleichtert. Damit der Updater die Differenzen zwischen Client und Server berechnen kann benötigt er eine version.txt, die den neusten Stand der Dateien wiederspiegelt. In dieser Datei befinden sich die Checksummen, Größenangaben, Pfade und Zeitstempel der einzelnen Dateien.

## Build
Um diese Anwendung zu bauen muss zuerst das Projekt geklont werden. Dazu wird der folgende Befehl auf der Kommandozeile ausgeführt.
```
git clone https://git.darkatra.de/DarkAtra/Patcher.git
```
Um das Projekt nun zu bauen wird der folgende Befehl auf der Kommandozeile ausgeführt.
```
mvn clean package
```
Das wars. Die Jar-Dateien befinden sich nun im target-Ordner der jeweiligen Anwendung. Bsp.: `updater/target/updater-0.0.1-SNAPSHOT.jar`

## Develop
Sollte das Projekt nicht nicht geklont sein muss der folgende Befehl ausgeführt werden.
```
git clone https://git.darkatra.de/DarkAtra/Patcher.git
```
Im Anschluss kann eine beliebige IDE (z.B. Eclipse, IntelliJ) gestartet und das Maven-Projekt importiert werden. Benutzen Sie dazu den Import-Dialog der IDE.
Das wars. Hinweis: Unter Umständen kann das Kompilieren mit JDK 9+ fehlschlagen. Empfohlen wird JDK 8.