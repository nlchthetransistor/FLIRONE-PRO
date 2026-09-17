#!/bin/bash
set -e
cd "$(dirname "$0")"
CONFIG="flirone_config.json"
echo "Starting FLIRONE v2..."
echo "Web UI: http://$(hostname -I | awk '{print $1}'):8080"
sudo ./flirone "$CONFIG" 2>&1 | tee -a flirone.log
