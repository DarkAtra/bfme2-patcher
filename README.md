# bfme2-patcher

The `bfme2-patcher` consists of the following applications:

- **game-patcher**:
  The `game-patcher` aims to patch certain aspects of the original game executable to allow proper online gameplay. Currently work in progress.
- **map-builder**:
  The `map-builder` is an application that helps with building the `!map.big` archive. It adjusts the camera settings for each map, generates the `mapcache.ini`
  and creates the final big archive.
- **mod-builder**:
  The `mod-builder` is an application that helps with bundling a new version of the mod. It basically takes the necessary files and puts them in dedicated big
  archives for release.
- **update-builder**:
  The `update-builder` is an application that makes it easy to create a `version.json`. The updater needs this file to calculate differences between client and
  server. This file contains the checksums, size specifications, paths and timestamps of the individual files.
- **updater**:
  The `updater` keeps all files for a program and itself up to date. It calculates the differences between the current version of the program and the latest
  version from the server. For this purpose a SHA-3 hash is created for each file and compared with the server's hash. Afterwards all outdated or damaged files
  are downloaded from the server and obsolete files are deleted.
- **updater-ifeo**:
  The `updater-ifeo` is a tiny utility that is used to set the `Debugger` registry key for the games. This allows the updater to launch instead of the game.

## Build

Clone the project:

```
git clone git@github.com:DarkAtra/bfme2-patcher.git
```

Build the project using the following command:

```
gradlew clean test jar createExe
```

The jar and exe files are located in the `build` folders of the respective applications.
