# bfme2-patcher

The `bfme2-patcher` consists of the following applications:

- **map-builder**:
  The `map-builder` is an application that helps with building the `!map.big` archive. It takes all the necessary files for each map, generates
  the `mapcache.ini`
  and creates the final big archive.
- **map-compressor**:
  The `map-compressor` is an application that compresses all maps in a given folder using the `deflate` compression format.
- **mod-builder**:
  The `mod-builder` is an application that helps with bundling a new version of the mod. It basically takes the necessary files and puts them in dedicated big
  archives for release.
- **update-builder**:
  The `update-builder` is an application that makes it easy to create a `version.json`. The updater needs this file to calculate differences between client and
  server. This file contains the checksums, size specifications, paths and timestamps of the individual files.
- **updater**:
  The `updater` keeps all files for a program and itself up to date. It calculates the differences between the current version of the program and the latest
  version from the server. For this purpose a SHA-3 hash is created for each file and compared with the server's hash. Afterwards all newer or damaged files are
  downloaded from the server and old files are deleted.

## Build

Clone the project:

```
git clone git@github.com:DarkAtra/bfme2-patcher.git
```

Build the project using the following command:

```
gradlew clean packageUberJarForCurrentOS
```

The jar files are located in the `build` folders of the respective applications. E.g.: `updater/build/updater-<os>-0.5.3.jar`
