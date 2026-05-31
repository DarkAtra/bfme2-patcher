# bfme2-patcher

The `bfme2-patcher` consists of the following applications:

- **asset-builder**:
  An application that helps with building the `asset.dat` file. It searches for the `art` folder in the current working directory and then builds the
  `mod_asset.dat` file for it. Afterwards, it merges the `bfme2_orig_asset.dat` and the `mod_asset.dat` into a single `asset.dat` file. The resulting
  `asset.dat` should be used by the game.
- **game-patcher**:
  Aims to patch certain aspects of the original game executable to allow proper online gameplay. It redirects DNS queries for GameSpy and EA
  specific hosts to servers that are maintained by the community ([t3a:online](https://t3aonline.net/)). It also disables the games SSL certificate verification
  so that a connection to the community servers can be established.
- **map-builder**:
  An application that helps with building the `!map.big` archive. It adjusts the camera settings for each map, generates the `mapcache.ini`
  and creates the final big archive.
- **mod-builder**:
  An application that helps with bundling a new version of the mod. It basically takes the necessary files and puts them in dedicated big
  archives for release.
- **update-builder**:
  An application that makes it easy to create a `version.json`. The updater needs this file to calculate differences between client and
  server. This file contains the checksums, size specifications, paths and timestamps of the individual files.
- **updater**:
  Keeps all files for a program and itself up to date. It calculates the differences between the current version of the program and the latest
  version from the server. For this purpose a SHA-3 hash is created for each file and compared with the server's hash. Afterwards, all outdated or damaged files
  are downloaded from the server and obsolete files are deleted.

## Build

Clone the project:

```
git clone git@github.com:DarkAtra/bfme2-patcher.git
```

Build the project using the following command:

```
./gradlew clean test build jar createExe
```

The jar and exe files are located in the `build` folders of the respective applications.
