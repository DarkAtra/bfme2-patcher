# Patcher
The Patcher consists of two applications:
- **Updater**:
The updater keeps all files for a program up to date.
It calculates the differences between the current version of the program and the latest version from the server.
For this purpose a SHA-3 hash is created for each file and compared with the server's hash.
Afterwards all newer or damaged files are downloaded from the server and old files are deleted.
- **Update-Builder**:
The Update-Builder is an application that makes it easy to create a `version.json`.
The updater needs this file to calculate differences between client and server.
This file contains the checksums, size specifications, paths and timestamps of the individual files.

## Build
Clone the project:
```
git clone https://git.darkatra.de/DarkAtra/Patcher.git
```
Build the project using the following command:
```
mvn clean install
```
The jar files are located in the target folders of the respective applications. E.g.: `updater/target/updater.jar`

## Build Native - Requirements

### Mac OS X and iOS

* Download the latest release version of GraalVM: https://github.com/graalvm/graalvm-ce-builds/releases/tag/vm-20.2.0 by choosing `graalvm-ce-java11-darwin-amd64-20.2.0.tar.gz` from the list of assets and unpack it to a preferred location on your system (e.g. in `/opt`)

* Configure the runtime environment. Set `GRAALVM_HOME` environment variable to the GraalVM installation directory.

  For example:

      export GRAALVM_HOME=/opt/graalvm-ce-java11-20.2.0/Contents/Home

* Set `JAVA_HOME` to point to the GraalVM installation directory:

      export JAVA_HOME=$GRAALVM_HOME

#### Additional requirements

* iOS can be built only on Mac OS X

* Xcode 11+ is required to build for iOS 13+. Install `Xcode` from the [Mac App Store](https://apps.apple.com/us/app/xcode/id497799835?mt=12) if you haven't already. 

* Install `Homebrew`, if you haven't already. Please refer to https://brew.sh/ for more information.

* Install `libusbmuxd`

  Using `brew`:

      brew install --HEAD libusbmuxd

* Install `libimobiledevice`

  Using `brew`:

      brew install --HEAD libimobiledevice

### Linux and Android

* Download the latest release version of GraalVM: https://github.com/graalvm/graalvm-ce-builds/releases/tag/vm-20.2.0 by choosing `graalvm-ce-java11-linux-amd64-20.2.0.tar.gz` from the list of assets and unpack it to a preferred location on your system (e.g. in `/opt`)

* Configure the runtime environment. Set `GRAALVM_HOME` environment variable to the GraalVM installation directory.

  For example:

      export GRAALVM_HOME=/opt/graalvm-ce-java11-20.2.0

* Set `JAVA_HOME` to point to the GraalVM installation directory:

      export JAVA_HOME=$GRAALVM_HOME

#### Additional requirements

* Android can be built only on Linux OS

The client plugin will download the Android SDK and install the required packages. 

Alternatively, you can define a custom location to the Android SDK by setting the `ANDROID_SDK` environment variable, making sure that you have installed all the packages from the following list:

* platforms;android-28
* platform-tools
* build-tools;29.0.2
* extras;android;m2repository
* extras;google;m2repository
* ndk-bundle (in case you opt to skip this bundle and download Android NDK package separately, set the `ANDROID_NDK` environment variable to its location)

### Windows

* Download the latest release version of GraalVM: https://github.com/graalvm/graalvm-ce-builds/releases/tag/vm-20.2.0 by choosing `graalvm-ce-java11-windows-amd64-20.2.0.zip` from the list of assets and unzip it to a preferred location on your system.

* Make sure you have installed Visual Studio 2019 with the following components:
  - Choose the English Language Pack
  - C++/CLI support for v142 build tools (14.25 or later)
  - MSVC v142 - VS 2019 C++ x64/x86 build tools (v14.25 or later)
  - Windows Universal CRT SDK
  - Windows 10 SDK (10.0.19041.0 or later)

* Run the maven commands mentioned below in a `x64 Native Tools Command Prompt for VS 2019`. This command prompt can be accessed
from the start menu.

* Configure the runtime environment. Set `GRAALVM_HOME` environment variable to the GraalVM installation directory.

  For example:

      set GRAALVM_HOME=C:\tools\graalvm-ce-java11-20.2.0

* Set `JAVA_HOME` to point to the GraalVM installation directory:

      set JAVA_HOME=%GRAALVM_HOME%

## Build Native - The actual build step [WIP]
Make sure GraalVM is used for Maven.

```
mvn -Pnative clean package
```

This will only try to natively compile the `updater` module. You can find the resulting application in the target folder.

## Develop
Clone the project:
```
git clone https://git.darkatra.de/DarkAtra/Patcher.git
```
Afterwards, you can start any IDE (e.g., Eclipse, IntelliJ) and import the Maven project. Use the import dialog of the IDE to do this.
Note: This project requires at least JDK 9 because it already uses the module system of Java (Jigsaw).
