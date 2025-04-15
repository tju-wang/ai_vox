#!/bin/bash

arduino-cli compile -b esp32:esp32:esp32s3 $1 -v --board-options "PartitionScheme=huge_app" --board-options "PSRAM=opi" --build-property "build.extra_flags=-Wall -Werror -DWIFI_SSID=\"emakefun\" -DWIFI_PASSWORD=\"501416wf\" -DPRINT_HEAP_INFO_INTERVAL=1000 -DCLOGGER_SEVERITY=0"