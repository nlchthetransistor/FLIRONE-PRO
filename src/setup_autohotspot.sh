#!/bin/bash
# ==============================================================================
# FLIRONE AutoHotspot Setup Script for Raspberry Pi 4
# Tự động chuyển đổi giữa Wi-Fi Client và Access Point (Hotspot)
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
echo "    CÀI ĐẶT AUTOHOTSPOT CHO CAMERA HỒNG NGOẠI FLIRONE     "
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
    
    # Xoá profile cũ nếu tồn tại
    nmcli connection delete "FLIRONE-Hotspot" 2>/dev/null || true

    # Tạo Hotspot AP mới
    nmcli connection add type wifi ifname wlan0 con-name "FLIRONE-Hotspot" autoconnect no ssid "$AP_SSID"
    nmcli connection modify "FLIRONE-Hotspot" 802-11-wireless.mode ap 802-11-wireless.band bg
    nmcli connection modify "FLIRONE-Hotspot" wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$AP_PASS"
    nmcli connection modify "FLIRONE-Hotspot" ipv4.method shared ipv4.addresses "$AP_IP/24"

    # Tạo script kiểm tra kết nối định kỳ
    cat << 'EOF' > /usr/local/bin/autohotspot-check.sh
#!/bin/bash
AP_NAME="FLIRONE-Hotspot"

# Đợi interface wlan0 sẵn sàng
sleep 15

# Kiểm tra xem wlan0 có đang kết nối vào mạng Wi-Fi nào không (trừ Hotspot chính nó)
ACTIVE_CON=$(nmcli -t -f NAME,DEVICE con show --active | grep ":wlan0" | cut -d: -f1 || true)

if [ -z "$ACTIVE_CON" ]; then
    echo "[AutoHotspot] Không có kết nối Wi-Fi khả dụng. Đang kích hoạt Hotspot AP..."
    nmcli connection up "$AP_NAME"
elif [ "$ACTIVE_CON" == "$AP_NAME" ]; then
    echo "[AutoHotspot] Đang ở chế độ Hotspot AP."
else
    echo "[AutoHotspot] Đã kết nối vào Wi-Fi: $ACTIVE_CON. Không cần bật Hotspot."
fi
EOF

    chmod +x /usr/local/bin/autohotspot-check.sh

else
    # --------------------------------------------------------------------------
    # CẤU HÌNH DÀNH CHO DHCPCD + HOSTAPD + DNSMASQ (BULLSEYE)
    # --------------------------------------------------------------------------
    echo "[*] Đang cài đặt hostapd và dnsmasq..."
    apt-get update -y
    apt-get install -y hostapd dnsmasq iw

    systemctl unmask hostapd 2>/dev/null || true
    systemctl disable hostapd dnsmasq 2>/dev/null || true

    # Cấu hình hostapd
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

    # Cấu hình dnsmasq
    cat << EOF > /etc/dnsmasq.conf
interface=wlan0
dhcp-range=192.168.4.10,192.168.4.50,255.255.255.0,24h
EOF

    # Script autohotspot cho dhcpcd
    cat << 'EOF' > /usr/local/bin/autohotspot-check.sh
#!/bin/bash
sleep 15

# Kiểm tra nếu wlan0 đã có IP hợp lệ (đã bắt được Wi-Fi ngoài)
WIFI_IP=$(ip -4 addr show wlan0 | grep -oP '(?<=inet\s)\d+(\.\d+){3}' || true)

if [ -n "$WIFI_IP" ] && [[ "$WIFI_IP" != 192.168.4.* ]]; then
    echo "[AutoHotspot] Đã có kết nối Wi-Fi ($WIFI_IP). Giữ nguyên chế độ Client."
    systemctl stop hostapd 2>/dev/null || true
    systemctl stop dnsmasq 2>/dev/null || true
else
    echo "[AutoHotspot] Không tìm thấy Wi-Fi ngoài. Bật Hotspot AP (192.168.4.1)..."
    ip link set dev wlan0 down
    ip addr flush dev wlan0
    ip link set dev wlan0 up
    ip addr add 192.168.4.1/24 dev wlan0
    systemctl start dnsmasq
    systemctl start hostapd
fi
EOF

    chmod +x /usr/local/bin/autohotspot-check.sh
fi

# 2. Tạo Systemd Service để tự động chạy khi khởi động
echo "[*] Đang đăng ký Systemd Service autohotspot.service..."
cat << EOF > /etc/systemd/system/autohotspot.service
[Unit]
Description=FLIRONE AutoHotspot Service
After=network.target
Wants=network.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/autohotspot-check.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable autohotspot.service

echo ""
echo "=========================================================="
echo "    ✓ CÀI ĐẶT AUTOHOTSPOT HOÀN TẤT THÀNH CÔNG!           "
echo "=========================================================="
echo "Kể từ bây giờ:"
echo "1. Nếu RPi bắt được Wi-Fi quen thuộc -> Nó sẽ kết nối bình thường."
echo "2. Nếu mang đến nơi không có Wi-Fi:"
echo "   - RPi sẽ phát Wi-Fi tên: '$AP_SSID'"
echo "   - Mật khẩu: '$AP_PASS'"
echo "   - Mở điện thoại/máy tính kết nối vào Wi-Fi trên"
echo "   - Mở trình duyệt truy cập: http://$AP_IP:8080"
echo "=========================================================="
