#!/bin/bash
set -e

cd /home/pi/v4l2loopback/flirone-v4l2/src

PALLETE="/home/pi/v4l2loopback/flirone-v4l2/palettes/Iron2.raw"

./flirone "$PALETTE"  >> /home/pi/flirone_run.log 2>&1


