# Build from pre-built CRSDK binary files

This package should have the following structure.

## Directory structure
```
.
├── app
│   ├── <App source files>
│   └── CRSDK
│       └── <Public headers>
├── cmake
│   ├── enum_cli_hdr.cmake
│   ├── enum_cli_src.cmake
│   └── enum_crsdk_hdr.cmake
├── CMakeLists.txt
├── external
│   └── crsdk
│       ├── CrAdapter
│       │   ├── Cr_PTP_IP binary
│       │   ├── Cr_PTP_USB binary
│       │   └── libusb-1.0 binary
│       └── Cr_Core binary
└── README.md
```

## Install required libraries and tools
### Linux
The package versions included in 18.04 LTS will work.
```
sudo apt install autoconf libtool libudev-dev gcc g++ make cmake unzip libxml2-dev
```

### Windows 10
Install the following:
* Visual Studio 2017 or later
* Visual Studio Toolset v141
* Windows SDK 10.0.17763.0
* libusbK 3.0 driver
* CMake

### Mac
* macOS 10.14 (Mojave) / 10.15 (Catalina) / 11.1 or later (Big Sur)
* Xcode 11.3 / 11.3.1 / 11.5 (for macOS 11.1 or later)
* Packages:
```
brew install cmake autoconf automake libtool
```

## Generate build files and build using CMake
__Note1: The generated build files cannot be moved from the directory
they are generated in due to CMake using absolute paths.
Generate the build files in the directory you wish to build from.
Check the CMake documentation to see how to specify a different build directory__

__Note2: The build result can be moved without issue__
```
linux:
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    cmake --build .
```

```
windows:
    mkdir build
    cd build
    cmake -A "x64" -T "v141,host=x64" ..
    open the VS project file and build from it
```

```
mac:
    mkdir build
    cd build
    cmake -GXcode ..
    open the Xcode project file and build from it
```
