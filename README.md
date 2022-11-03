### Introduction

This project is a fork of the Microchip's WFI32-IoT_Azure_PnP_Demo 
modified to support IoTConnect. This project is not an SDK, and it is 
intended to be used as a demo.

The project demonstrates the use of WFI32-IoT board's built-in 
secure element to connect to IoTConnect and relay the VAV Press Click 
and Ultra-Low Press Click boards.

### Hardware Setup With Sensors

This project can run on a standalone WFI32-IoT board. 
If you wish to add support for telemetry from pressure Clik Boards, 
simply follow the instructions at the original project's at
[Adding Extra Sensors](https://github.com/MicrochipTech/WFI32-IoT_Azure_PnP_Demo#adding-extra-sensors-to-the-wfi32-iot-board)
section to set up the hardware.

### Initial Setup
* Clone this repo using Got command line:
```shell
git clone --recurse-submodules <http://this/repo/URL>
```
* Install Microchip `MPLAB X` toolchain for embedded code development on 32-bit architecture MCU/MPU platforms:

    - [MPLAB X IDE](https://www.microchip.com/mplab/mplab-x-ide)

    - [MPLAB XC32 Compiler](https://www.microchip.com/en-us/development-tools-tools-and-software/mplab-xc-compilers#tabs)
    
NOTE: This demo was tested successfully with MPLab X IDE 6.0 and XC32 v4.10, and in general should work with later versions of the compiler as they become available. If you encounter issues building the project, it is recommended to download XC32 v4.10 from the [MPLAB Development Ecosystem Downloads Archive](https://www.microchip.com/en-us/tools-resources/archives/mplab-ecosystem) (to fall back to the version Microchip successfully tested prior to release). 

### Project Build

* Open MPLab X IDE.
* Load the project from the [firmware/WFI32-IoT_Azure.X](firmware/WFI32-IoT_Azure.) directory with MPLab IDE.
* Debug the project from the menu via Debug->Debug Project,
or by clicking the Debug Project button in the toolbar. We need to access the
mass storage device on the USB interface in order to obtain the device
certificate in the next step.
* Once the device starts running, a mass storage device should appear after some time. 
Note the device certificate in the file *snXXXXXXXXXXXXXXXXXX_device.pem*.
* Use that device certificate file as an argument to openssl the following command:

```shell
openssl x509 -noout -fingerprint  -inform pem -in path/to/snXXXXXXXXXXXXXXXXXX_device.pem 
```

* The openssl command will print the fingerprint (thumbprint) of the certificate. 
You will need to copy that value without the colons into the device IoTConnect setup 
step below.
* Alternatively you can use an online site like [this one](https://www.samltool.com/fingerprint.php)
to extract the fingerprint. Though leaking device certificates alone would not allow 
an attacker to mimic your device, other data from the device certificate could potentially be used.

### IoTConnect Device Setup

* In IoTConnect web UI, create a new device template using Self-Signed authentication type
with 1.0 device protocol version. 
* Add the following attributes as *NUMBER* values to your template.
  * For WFI32-IoT board's built-in sensors:
    * WFI32IoT_temperature
    * WFI32IoT_light
  * If using VAV Press Click board:
    * VAV_temperature
    * VAV_pressure
  * If using the Ultra-Low Press Click board:
    * ULP_temperature
    * ULP_pressure
* Create a new device based on your newly created template. Enter the 
certificate fingerprint obtained in the previous steps as Primary and Secondary Thumbprint in the
device creation form.
* Edit the **CLOUD.CFG** file on the board's USB Mass Storage device
and enter the value from your device's Info -> Connection Information  -> MQTT -> 
clientID as Registration ID in the file. 
* Edit [sample_config.h](./firmware/src/azure_rtos_demo/sample_azure_iot_embedded_sdk/sample_config.h) and enter the following values:
  - HOST_NAME: IoTHub host value from device's Info -> Connection Information  -> MQTT page.
  - IOTCONNECT_DTG: Template DTG value at the device Info page
  - IOTCONNECT_ENV: From sidebar Settings -> Key Vault - Environment value.
* Navigate to Settings -> Key Vault and enter the Environment value from that page
as IOTCONNECT_ENV into [sample_config.h](./firmware/src/azure_rtos_demo/sample_azure_iot_embedded_sdk/sample_config.h)
* Edit the **WIFI.CFG** file on the board's USB Mass Storage device 
per your WiFi credentials. The file should look like one of the following examples: 
    - Open Unsecured Network (no password protection)
        ```bash
        CMD:SEND_UART=wifi MY_SSID,,1
        ```
    - Wi-Fi Protected Access 2 (WPA2)
        ```bash
        CMD:SEND_UART=wifi MY_SSID,MY_PSWD,2
        ```
    - Wired Equivalent Privacy (WEP)
        ```bash
        CMD:SEND_UART=wifi MY_SSID,MY_PSWD,3
        ```
    - Wi-Fi Protected Access 3 (WPA3)
        ```bash
        CMD:SEND_UART=wifi MY_SSID,MY_PSWD,4
        ```
* At this point the project is ready to be rebuilt and loaded onto the board with newly configured values.

NOTE: When editing files on the USB Mass Storage deice, 
sometimes the OS may cache the changes you have made, 
so that the changes do not actually get written onto the device. You can either 
wait some time or you can "Safely Eject" the mass storage device before resetting the board.
In either case, you should check that changes took effect once the device comes back from a reset.
