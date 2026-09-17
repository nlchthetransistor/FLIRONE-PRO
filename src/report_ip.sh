#!/bin/bash
SHEETS_URL="https://script.google.com/macros/s/AKfycbwQgX8gRSEfPu7nOlI7V4Gvt0DFfGMvnor3y-ksJgtPWNWjuvkrVHmCQasf6xcH4qms/exec"

echo "[report_ip] Waiting for network..."
for i in $(seq 1 30); do
    if ping -c 1 8.8.8.8 &>/dev/null; then
        break
    fi
    sleep 1
done

IP=$(hostname -I | awk '{print $1}')
SSID=$(iwgetid -r 2>/dev/null || echo "Unknown")
HOSTNAME=$(hostname)
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
MAC=$(cat /sys/class/net/wlan0/address 2>/dev/null || echo "Unknown")

echo "[report_ip] IP=$IP, SSID=$SSID"

curl -s -L -o /dev/null \
    -H "Content-Type: application/json" \
    -d "{
        \"timestamp\": \"$TIMESTAMP\",
        \"patient_name\": \"SYSTEM\",
        \"patient_age\": \"\",
        \"patient_gender\": \"\",
        \"measurement_type\": \"boot\",
        \"temperature\": 0,
        \"emissivity\": 0,
        \"refl_offset\": 0,
        \"raw_scale\": 0,
        \"temp_offset\": 0,
        \"ambient_temp\": 0,
        \"humidity\": 0,
        \"ip\": \"$IP\",
        \"ssid\": \"$SSID\",
        \"hostname\": \"$HOSTNAME\",
        \"mac\": \"$MAC\"
    }" \
    "$SHEETS_URL"

echo "[report_ip] Done. Access: http://$IP:8080"