#!/bin/bash
# Build and run flirone with servo support

set -e

echo "=== Building Servo Library ==="
cd components/servo
make clean
make all
echo "✓ Servo library built successfully"
cd ../..

echo ""
echo "=== Building Flirone Application ==="
make all

echo ""
echo "=== Build Complete! ==="
echo ""
echo "To run flirone with servo control:"
echo "  sudo ./flirone ../palettes/Iron2.raw"
echo ""
echo "Servo Control:"
echo "  , (comma)   - Decrease servo angle (left)"
echo "  . (period)  - Increase servo angle (right)"
echo "  1-4         - Select tuning parameter"
echo "  +/-         - Adjust tuning parameter"
echo "  Left/Right  - Alternative tuning control"
echo "  ESC         - Exit program"
echo ""
echo "GPIO Pin Configuration:"
echo "  GPIO 17     - Servo control pin (adjust SERVO_GPIO_PIN in flirone.cpp if needed)"
echo ""
echo "Servo Range:"
echo "  0°          - Leftmost position"
echo "  90°         - Center/neutral position (initial)"
echo "  180°        - Rightmost position"
