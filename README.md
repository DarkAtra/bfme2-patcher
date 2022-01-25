# Patcher

The Patcher consists of 3 applications:

- **Updater**:
  The updater keeps all files for a program and itself up to date. It calculates the differences between the current version of the program and the latest
  version from the server. For this purpose a SHA-3 hash is created for each file and compared with the server's hash. Afterwards all newer or damaged files are
  downloaded from the server and old files are deleted.
- **Update-Builder**:
  The Update-Builder is an application that makes it easy to create a `version.json`. The updater needs this file to calculate differences between client and
  server. This file contains the checksums, size specifications, paths and timestamps of the individual files.
- **Map-Builder**:
  The Map-Builder is an application that helps with building the `!map.big` archive. It takes all the necessary files for each map, generates the `mapcache.ini`
  and creates the final big archive.
- **Map-Compressor**:
  The Map-Compressor is an application that compresses all maps in a given folder using the `deflate` compression format.
- **Mod-Builder**:
  The Mod-Builder is an application that helps with bundling a new version of the mod. It basically takes the necessary files and puts them in dedicated big
  archives for release.

## Build

Clone the project:

```
git clone git@github.com:DarkAtra/bfme2-patcher.git
```

Build the project using the following command:

```
mvn clean install
```

The jar files are located in the target folders of the respective applications. E.g.: `updater/target/updater.jar`

## Develop

Clone the project:

```
git clone git@github.com:DarkAtra/bfme2-patcher.git
```

Afterwards, you can start any IDE (e.g., Eclipse, IntelliJ) and import the Maven project. Use the import dialog of the IDE to do this. Note: This project
requires at least JDK 9 because it already uses the module system of Java (Jigsaw).
