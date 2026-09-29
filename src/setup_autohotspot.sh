#!/bin/bash
# ==============================================================================
# FLIRONE AutoHotspot Watchdog Daemon for Raspberry Pi 4
# - Mạng ngoài (Client): Tự do kết nối 2.4GHz HOẶC 5GHz (như Vien VLYS 5580 MHz)
# - Khi mất mạng / đến phòng mới: Tự động kích hoạt Hotspot 'FLIRONE-CAM' trên wlan0
# - Watchdog giám sát liên tục mỗi 15 giây
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
echo "    CÀI ĐẶT AUTOHOTSPOT WATCHDOG (STANDALONE FALLBACK)    "
echo "    SSID: $AP_SSID                                        "
echo "    Mật khẩu: $AP_PASS                                    "
echo "    IP Access Point: $AP_IP                               "
echo "=========================================================="

# 1. Dọn dẹp hoàn toàn cấu hình thử nghiệm Dual Wi-Fi (uap0) cũ
echo "[1/4] Dọn dẹp card ảo uap0 và khôi phục cài đặt gốc..."
systemctl stop uap0.service 2>/dev/null || true
systemctl disable uap0.service 2>/dev/null || true
rm -f /etc/systemd/system/uap0.service
rm -f /usr/local/bin/create_uap0.sh
iw dev uap0 del 2>/dev/null || true
nmcli connection delete "FLIRONE-DualAP" 2>/dev/null || true

# Khôi phục cài đặt mạng Wi-Fi ngoài (cho phép tự do bắt 5GHz tốc độ cao như cũ)
if command -v nmcli &> /dev/null; then
    nmcli connection modify "Vien VLYS" 802-11-wireless.band "" 2>/dev/null || true
    echo "[+] Đã khôi phục mạng 'Vien VLYS' về băng tần 5GHz mặc định."
fi

# 2. Phát hiện hệ điều hành (NetworkManager vs dhcpcd)
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
    echo "[2/4] Đang cấu hình profile Hotspot chuẩn trên wlan0..."
    
    nmcli connection delete "FLIRONE-Hotspot" 2>/dev/null || true

    # Tạo Hotspot trên wlan0 với chuẩn WPA2-CCMP tương thích tuyệt đối mọi thiết bị
    nmcli connection add type wifi ifname wlan0 con-name "FLIRONE-Hotspot" autoconnect no ssid "$AP_SSID"
    nmcli connection modify "FLIRONE-Hotspot" 802-11-wireless.mode ap 802-11-wireless.band bg
    nmcli connection modify "FLIRONE-Hotspot" wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$AP_PASS"
    nmcli connection modify "FLIRONE-Hotspot" wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp
    nmcli connection modify "FLIRONE-Hotspot" ipv4.method shared ipv4.addresses "$AP_IP/24"

    # Tạo script Watchdog Daemon chạy ngầm
    echo "[3/4] Đang tạo Watchdog Daemon kiểm tra kết nối liên tục..."
    cat << 'EOF' > /usr/local/bin/autohotspot-daemon.sh
#!/bin/bash
AP_NAME="FLIRONE-Hotspot"

# Đợi hệ thống khởi động ổn định lúc boot
sleep 20

echo "[AutoHotspot Daemon] Bắt đầu giám sát Wi-Fi wlan0..."

while true; do
    # Kiểm tra kết nối active trên wlan0
    ACTIVE_CON=$(nmcli -t -f NAME,DEVICE con show --active 2>/dev/null | grep ":wlan0" | cut -d: -f1 || true)

    if [ -z "$ACTIVE_CON" ]; then
        # Không có kết nối Wi-Fi nào trên wlan0 -> Tự động bật Hotspot
        echo "[AutoHotspot] Mất sóng Wi-Fi ngoài. Bật Hotspot '$AP_NAME'..."
        nmcli connection up "$AP_NAME" 2>/dev/null || true

    elif [ "$ACTIVE_CON" == "$AP_NAME" ]; then
        # Đang ở chế độ Hotspot:
        # Kiểm tra xem có thiết bị nào (điện thoại/laptop y tá) đang kết nối không
        CLIENT_COUNT=$(iw dev wlan0 station dump 2>/dev/null | grep -c "Station" || echo 0)
        
        if [ "$CLIENT_COUNT" -gt 0 ]; then
            # Đang có y tá/bác sĩ kết nối xem camera nhiệt -> Giữ nguyên kết nối
            :
        else
            # Không có ai kết nối vào Hotspot: Thử quét và kết nối lại mạng Wi-Fi đã lưu (sau mỗi 60s)
            nmcli device wifi rescan 2>/dev/null || true
            nmcli device connect wlan0 2>/dev/null || true
        fi
    else
        # Đang kết nối bình thường với Wi-Fi ngoài (như Vien VLYS 5GHz) -> Giữ nguyên
        :
    fi

    sleep 15
done
EOF

    chmod +x /usr/local/bin/autohotspot-daemon.sh

else
    # --------------------------------------------------------------------------
    # CẤU HÌNH DÀNH CHO DHCPCD + HOSTAPD + DNSMASQ (BULLSEYE)
    # --------------------------------------------------------------------------
    echo "[2/4] Đang cài đặt hostapd và dnsmasq..."
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

echo "[AutoHotspot Daemon] Bắt đầu giám sát Wi-Fi..."

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

# 3. Đăng ký Watchdog Service với Systemd
echo "[4/4] Khởi động dịch vụ Watchdog autohotspot.service..."
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

# Kết nối lại mạng Wi-Fi ngoài nếu đang có
nmcli connection up "Vien VLYS" 2>/dev/null || true

echo ""
echo "=========================================================="
echo "    ✓ HOÀN TẤT THIẾT LẬP AUTOHOTSPOT WATCHDOG             "
echo "=========================================================="
echo "1. Đã xóa bỏ card ảo uap0, chip Wi-Fi hoàn toàn ổn định."
echo "2. Mạng Wi-Fi ngoài (Vien VLYS): Tự do bắt sóng 5GHz tốc độ cao."
echo "3. Khi mang đến nơi mất Wi-Fi (hoặc tắt router):"
echo "   - Sau 15-20s, Pi tự động phát Wi-Fi: '$AP_SSID'"
echo "   - Mật khẩu: '$AP_PASS'"
echo "   - Địa chỉ truy cập: http://$AP_IP:8080"
echo "4. Khi có người kết nối xem camera, Hotspot sẽ giữ nguyên."
echo "=========================================================="
