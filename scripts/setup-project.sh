#!/bin/bash

cd "`dirname $0`/.."

set -e
git update-index --assume-unchanged firmware/src/azure_rtos_demo/sample_azure_iot_embedded_sdk/sample_device_identity.c
git update-index --assume-unchanged firmware/src/azure_rtos_demo/sample_azure_iot_embedded_sdk/sample_config.h
echo Done
