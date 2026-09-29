#!/bin/bash
# ==============================================================================
# FLIRONE AutoHotspot Watchdog Daemon for Raspberry Pi 4
# Giám sát mạng liên tục: Nếu mất Wi-Fi giữa chừng -> Tự động bật lại Hotspot
# Tương thích cả Raspberry Pi OS Bookworm (NetworkManager) & Bullseye (dhcpcd)
# ==============================================================================

set -e

if [ "$EUID" -ne 0 ]; then
    echo "[-] Vui lòng chạy script này với quyền sudo: sudo ./setup_autohotspot.sh"
    exit 1
fi

AP_SSID="FLIRONE-CAM"
AP_PASS="flirone123"
AP_IP="192.168.4.1"

echo "=========================================================="
echo "    CÀI ĐẶT AUTOHOTSPOT WATCHDOG CHO CAMERA FLIRONE       "
echo "    SSID: $AP_SSID                                        "
echo "    Mật khẩu: $AP_PASS                                    "
echo "    IP Access Point: $AP_IP                               "
echo "=========================================================="

# 1. Phát hiện hệ điều hành đang dùng NetworkManager (Bookworm) hay dhcpcd (Bullseye)
if command -v nmcli &> /dev/null && systemctl is-active --quiet NetworkManager; then
    USE_NM=1
    echo "[+] Phát hiện Raspberry Pi OS Bookworm (NetworkManager)"
else
    USE_NM=0
    echo "[+] Phát hiện Raspberry Pi OS Bullseye / Legacy (dhcpcd + hostapd)"
fi

if [ $USE_NM -eq 1 ]; then
    # --------------------------------------------------------------------------
    # CẤU HÌNH DÀNH CHO NETWORKMANAGER (BOOKWORM)
    # --------------------------------------------------------------------------
    echo "[*] Đang cấu hình profile Hotspot trong NetworkManager..."
    
    nmcli connection delete "FLIRONE-Hotspot" 2>/dev/null || true

    nmcli connection add type wifi ifname wlan0 con-name "FLIRONE-Hotspot" autoconnect no ssid "$AP_SSID"
    nmcli connection modify "FLIRONE-Hotspot" 802-11-wireless.mode ap 802-11-wireless.band bg
    nmcli connection modify "FLIRONE-Hotspot" wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$AP_PASS"
    nmcli connection modify "FLIRONE-Hotspot" ipv4.method shared ipv4.addresses "$AP_IP/24"

    # Tạo Daemon giám sát chạy ngầm liên tục
    cat << 'EOF' > /usr/local/bin/autohotspot-daemon.sh
#!/bin/bash
AP_NAME="FLIRONE-Hotspot"

# Đợi hệ thống khởi động ổn định
sleep 20

echo "[AutoHotspot Daemon] Bắt đầu giám sát kết nối Wi-Fi..."

while true; do
    # Lấy tên kết nối đang hoạt động trên wlan0
    ACTIVE_CON=$(nmcli -t -f NAME,DEVICE con show --active 2>/dev/null | grep ":wlan0" | cut -d: -f1 || true)

    if [ -z "$ACTIVE_CON" ]; then
        # Mất kết nối hoàn toàn -> Kích hoạt Hotspot
        echo "[AutoHotspot] Phát hiện mất Wi-Fi. Đang tự động kích hoạt Hotspot '$AP_NAME'..."
        nmcli connection up "$AP_NAME" 2>/dev/null || true
    elif [ "$ACTIVE_CON" == "$AP_NAME" ]; then
        # Đang ở chế độ Hotspot: Kiểm tra xem có thiết bị nào (điện thoại/tablet) đang kết nối không
        CLIENT_COUNT=$(iw dev wlan0 station dump 2>/dev/null | grep -c "Station" || echo 0)
        if [ "$CLIENT_COUNT" -gt 0 ]; then
            # Đang có y tá/bác sĩ kết nối xem camera -> Giữ nguyên, không làm gián đoạn
            :
        else
            # Không có ai kết nối vào Hotspot: Thử thăm dò xem có Wi-Fi quen thuộc trở lại không (sau mỗi 60s)
            nmcli device wifi rescan 2>/dev/null || true
        fi
    fi

    # Kiểm tra lại sau mỗi 15 giây
    sleep 15
done
EOF

    chmod +x /usr/local/bin/autohotspot-daemon.sh

else
    # --------------------------------------------------------------------------
    # CẤU HÌNH DÀNH CHO DHCPCD + HOSTAPD + DNSMASQ (BULLSEYE)
    # --------------------------------------------------------------------------
    echo "[*] Đang cài đặt hostapd và dnsmasq..."
    apt-get update -y
    apt-get install -y hostapd dnsmasq iw

    systemctl unmask hostapd 2>/dev/null || true
    systemctl disable hostapd dnsmasq 2>/dev/null || true

    cat << EOF > /etc/hostapd/hostapd.conf
interface=wlan0
driver=nl80211
ssid=$AP_SSID
hw_mode=g
channel=7
wmm_enabled=0
macaddr_acl=0
auth_algs=1
ignore_broadcast_ssid=0
wpa=2
wpa_passphrase=$AP_PASS
wpa_key_mgmt=WPA-PSK
wpa_pairwise=TKIP
rsn_pairwise=CCMP
EOF

    cat << EOF > /etc/dnsmasq.conf
interface=wlan0
dhcp-range=192.168.4.10,192.168.4.50,255.255.255.0,24h
EOF

    cat << 'EOF' > /usr/local/bin/autohotspot-daemon.sh
#!/bin/bash
sleep 20

echo "[AutoHotspot Daemon] Bắt đầu giám sát kết nối Wi-Fi..."

while true; do
    WIFI_IP=$(ip -4 addr show wlan0 2>/dev/null | grep -oP '(?<=inet\s)\d+(\.\d+){3}' || true)

    if [ -z "$WIFI_IP" ]; then
        echo "[AutoHotspot] Mất kết nối Wi-Fi. Đang bật Hotspot AP (192.168.4.1)..."
        ip link set dev wlan0 down
        ip addr flush dev wlan0
        ip link set dev wlan0 up
        ip addr add 192.168.4.1/24 dev wlan0
        systemctl start dnsmasq
        systemctl start hostapd
    fi

    sleep 15
done
EOF

    chmod +x /usr/local/bin/autohotspot-daemon.sh
fi

# 2. Tạo Systemd Service chạy liên tục (Daemon mode)
echo "[*] Đang cấu hình Systemd Watchdog Service..."
cat << EOF > /etc/systemd/system/autohotspot.service
[Unit]
Description=FLIRONE AutoHotspot Watchdog Daemon
After=network.target
Wants=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/autohotspot-daemon.sh
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable autohotspot.service
systemctl restart autohotspot.service

echo ""
echo "=========================================================="
echo "    ✓ CÀI ĐẶT AUTOHOTSPOT WATCHDOG THÀNH CÔNG!           "
echo "=========================================================="
echo "Kể từ bây giờ:"
echo "1. Khi bật nguồn: Nếu không có Wi-Fi -> Tự phát AP sau 20s."
echo "2. KHI ĐANG CHẠY BỊ MẤT WI-FI: Hệ thống tự động chuyển sang"
echo "   phát Wi-Fi '$AP_SSID' sau tối đa 15 giây!"
echo "3. IP truy cập luôn là: http://$AP_IP:8080"
echo "=========================================================="
