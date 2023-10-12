# IoT-Connect Yocto Integration
***This IoT-Connect connect layer only supports `kirkstone`***

*The following details yocto layers designed to integrate the [IoT-Connect C SDK](https://github.com/avnet-iotconnect/iotc-generic-c-sdk). The end result is a compiled C binary that should successfully run & establish comms with an appropriately setup https://avnet.iotconnect.io/*

## Layers
There are 2 layers thus far: `meta-iotconnect` & `meta-myExampleIotconnectLayer`.
### `meta-iotconnect`
This layer draws in the various sources required to utilise the SDK. From a yocto perspective it largely serves to provide the sources to other recipes that produce executables e.g. `DEPENDS += "iotc-c-sdk"` will be present in said recipe.

```
iotc-yocto-c-sdk$ tree meta-iotconnect/
meta-iotconnect/
├── conf
│   └── layer.conf
└── recipes-apps
    └── iotConnect
        ├── files
        │   └── 0001_CMake_findPackage.patch
        ├── iotc-c-sdk_0.1.bb
        └── iotc-c-sdk_1.0.bb
```

### `meta-myExampleIotconnectLayer`
This layer provides an example of how a user might write a recipe suitable for their application. It contains a simple application that demonstrates telemetry. Once installed on the image it can be started by logging in & executing `/usr/bin/local/iotc/telemetry-demo /path/to/config.json` where `config.json` is a file that contains device authentication information and paths to where telemetry-demo will read data from on the host device. It's expected that in the 1st instance a user would run this demo on their hardware after editing a sample `config.json` to reflect a device they've defined on iotconnect.io and sensor data particular to their hardware.

By adding the recipe to your image (e.g. `IMAGE_INSTALL += " telemetry-demo"` in `conf/local.conf`)

```
iotc-yocto-c-sdk$ tree meta-myExampleIotconnectLayer/
meta-myExampleIotconnectLayer/
├── conf
│   └── layer.conf
└── recipes-apps
    └── telemetry-demo <-------------------- Recipe directory
        ├── files
        │   ├── cmke-src <------------------ A small CMake project
        │   │   ├── CMakeLists.txt
        │   │   ├── json_parser.c
        │   │   ├── json_parser.h
        │   │   └── main.c
        │   └── eg-private-repo-data <----- Location for certificates/keys & other config data for development purposes.
        │       ├── config.json
        │       ├── config-symmtrcKy.json
        │       └── config-x509.json
        └── telemetry-demo_0.1.bb <-------- Recipe
```

As developing a iotc application involves the use of private/secure data like keys/certificates and the user is expected to develop same application using SCM like git, it's worth taking a moment to be aware of risks of accidentlally uploading private data to places it shouldnt belong. The directory `eg-priviate-repo-data` seeks to provide a safe space to place sensitive data like device keys etc for development purposes only. When the user installs the _development_ version of the recipe (e.g. `IMAGE_INSTALL += " telemetry-demo-dev"` in `conf/local.conf`) any files within `eg-private-repo-data` will be installed in the rootfs of the image. The `.gitignore` settings for this repo are also configured to prevent accidental upload of *.pem or *.crt files.

This approach allows the user to develop their solution conveniently, then when it's time to provide production builds, the result would be a clean installation awaiting first time configuration post image flash.

Also in the `eg-private-repo-data` are sample JSON files, these are explained in more detail in the drop-down section below..

<details>
  <summary>JSON config files</summary>
  The config json provides a quick and easy way to provide a user's executable with the requisite device credentials for any connection and a convenient method of mapping sensors to iotc device attributes. The demo source provided will match an `attribute.name` to a path on the user's host where the relevant sensor data resides. It also indicates to the demo what format to expect the data at the path to be in.

```json
{
    "sdk_ver": "2.1",
    "duid": "Your Device's name in https://avnet.iotconnect.io/device/1",
    "cpid": "'CPID' from https://avnet.iotconnect.io/key-vault",
    "env": "'Environment' from https://avnet.iotconnect.io/key-vault",
    "iotc_server_cert": "/etc/ssl/certs/DigiCert_Global_Root_G2.pem",
    "sdk_id": "'SDK Identities -> Language: Python **, Version: 1.0' from https://avnet.iotconnect.io/key-vault",
    "auth": {
      "auth_type": "IOTC_AT_X509",
      "params": {
        "client_key": "/path/to/device.key",
        "client_cert": "/path/to/DeviceCertificate.pem"
      }
    },
    "device": {
      "commands_list_path": "Path to folder containing all commands",
      "offline_storage": {
        "available_space_MB": 1,
        "file_count": 1
      },
      "attributes": [
        {
          "name": "power",
          "private_data": "/usr/bin/local/iotc/dummy_sensor_power",
          "private_data_type": "ascii"
        },
        {
          "name": "level",
          "private_data": "/usr/bin/local/iotc/dummy_sensor_level",
          "private_data_type": "ascii"
        }
      ]
    }
}
```

The sample JSON contains key value pairs where the value contains directions to what your individual value will be. E.g:
```json
{
    "sdk_ver": "2.1",
    "duid": "Your Device's name in https://avnet.iotconnect.io/device/1",
```
Would become: 
```json
{
    "sdk_ver": "2.1",
    "duid": "myDemoDevice",
```
</details>

## How to include layers
To include the layers within a yocto enviroment:

1. check them out to the `sources` directory in your yocto enviroment.

1. add them to `conf/bblayers` file in your build directory

1. add recipes as a part of your image (for example in `<meta-my-layer>/recipes-core/images/<image-name.bb>` file) or to your local build configuration (in `local.conf` for example) - `IMAGE_INSTALL += " telemetry-demo-dev"`

1. using the config.json files in `eg-private-repo-data` as a template, create your own config.json with details of the device you have setup on iotconnect.io.

1. editing the same json as in the last step, edit the `attributes` section of the JSON so the `name` of the attritube maps to a path on your system where the relevant data can be found e.g. the path to the position data of an I2C accelerometer might be: `/sys/bus/i2c/devices/1-0053/position`.

1. build with a bitbake call e.g. `./bitbake <image-name>`

1. Flash the resultant image to the device.

2. Login into the device & run the command `/usr/bin/local/iotc/telemetry-demo /usr/local/iotc/config.json`

***Note***: you might need adding lines below to your image
```
inherit core-image
inherit module
inherit extrausers
```

## Board specific examples can be found [here](board_specific_readmes/README.md)
