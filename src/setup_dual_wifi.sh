#!/bin/bash
# ==============================================================================
# FLIRONE Dual Wi-Fi Setup Script (AP + STA Concurrency) for Raspberry Pi 4
# Cho phép vừa kết nối Wi-Fi ngoài (wlan0) vừa phát Access Point (uap0) đồng thời!
# ==============================================================================

set -e

if [ "$EUID" -ne 0 ]; then
    echo "[-] Vui lòng chạy với quyền sudo: sudo ./setup_dual_wifi.sh"
    exit 1
fi

AP_SSID="FLIRONE-CAM"
AP_PASS="flirone123"
AP_IP="192.168.4.1"

echo "=========================================================="
echo "    CÀI ĐẶT DUAL WI-FI (VỪA BẮT VỪA PHÁT) CHO RPI 4      "
echo "    Card bắt mạng (Client): wlan0                        "
echo "    Card phát mạng (AP):    uap0                         "
echo "    SSID: $AP_SSID | Pass: $AP_PASS | IP: $AP_IP          "
echo "=========================================================="

# 1. Cài đặt các gói phụ trợ cần thiết
echo "[1/5] Kiểm tra và cài đặt công cụ cần thiết (iw)..."
apt-get update -y
apt-get install -y iw rfkill

rfkill unblock wifi || true

# 2. Tắt dịch vụ AutoHotspot cũ (nếu đã cài trước đó để tránh xung đột)
if systemctl is-enabled autohotspot.service &>/dev/null; then
    echo "[2/5] Đang tắt dịch vụ autohotspot cũ..."
    systemctl stop autohotspot.service 2>/dev/null || true
    systemctl disable autohotspot.service 2>/dev/null || true
fi

# 3. Tạo script khởi tạo interface ảo uap0
echo "[3/5] Đang thiết lập script tạo interface ảo uap0..."
cat << 'EOF' > /usr/local/bin/create_uap0.sh
#!/bin/bash
# Đợi wlan0 sẵn sàng (tối đa 15s)
for i in $(seq 1 15); do
    if ip link show wlan0 &>/dev/null; then
        break
    fi
    sleep 1
done

if ! ip link show wlan0 &>/dev/null; then
    echo "Error: wlan0 not found"
    exit 1
fi

# Tạo interface uap0 nếu chưa tồn tại
if ! ip link show uap0 &>/dev/null; then
    echo "Creating virtual interface uap0..."
    iw dev wlan0 interface add uap0 type __ap

    # Tạo địa chỉ MAC riêng biệt cho uap0 (XOR byte đầu với 0x02)
    WLAN_MAC=$(cat /sys/class/net/wlan0/address)
    FIRST_BYTE=$(printf "%02x" $(( 0x${WLAN_MAC%%:*} ^ 2 )))
    REST_BYTES=${WLAN_MAC#*:}
    UAP_MAC="${FIRST_BYTE}:${REST_BYTES}"

    ip link set dev uap0 address "$UAP_MAC"
    ip link set dev uap0 up
    echo "uap0 created with MAC: $UAP_MAC"
fi
EOF

chmod +x /usr/local/bin/create_uap0.sh

# Tạo systemd service để tự tạo uap0 mỗi khi khởi động
cat << 'EOF' > /etc/systemd/system/uap0.service
[Unit]
Description=Create Virtual Wi-Fi AP Interface (uap0)
After=network-pre.target
Before=network.target NetworkManager.service dhcpcd.service
DefaultDependencies=no

[Service]
Type=oneshot
ExecStart=/usr/local/bin/create_uap0.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable uap0.service
/usr/local/bin/create_uap0.sh

# 4. Cấu hình phát Hotspot trên uap0
echo "[4/5] Đang cấu hình trạm phát Hotspot trên uap0..."

if command -v nmcli &> /dev/null && systemctl is-active --quiet NetworkManager; then
    echo "[+] Sử dụng NetworkManager (Raspberry Pi OS Bookworm)..."
    
    # Xoá profile cũ nếu có
    nmcli connection delete "FLIRONE-DualAP" 2>/dev/null || true

    # Tạo profile AP riêng gắn cứng vào interface ảo uap0
    nmcli connection add type wifi ifname uap0 con-name "FLIRONE-DualAP" autoconnect yes ssid "$AP_SSID"
    nmcli connection modify "FLIRONE-DualAP" 802-11-wireless.mode ap 802-11-wireless.band bg
    nmcli connection modify "FLIRONE-DualAP" wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$AP_PASS"
    nmcli connection modify "FLIRONE-DualAP" ipv4.method shared ipv4.addresses "$AP_IP/24"

    # Kích hoạt profile
    nmcli connection up "FLIRONE-DualAP" || true

else
    echo "[+] Sử dụng hostapd + dnsmasq (Raspberry Pi OS Bullseye/Legacy)..."
    apt-get install -y hostapd dnsmasq

    # Cấu hình IP tĩnh cho uap0 trong dhcpcd.conf
    if ! grep -q "interface uap0" /etc/dhcpcd.conf; then
        cat << EOF >> /etc/dhcpcd.conf

# FLIRONE Dual Wi-Fi AP
interface uap0
    static ip_address=$AP_IP/24
    nohook wpa_supplicant
EOF
    fi

    # Cấu hình dnsmasq cho uap0
    cat << EOF > /etc/dnsmasq.d/uap0.conf
interface=uap0
dhcp-range=192.168.4.10,192.168.4.50,255.255.255.0,24h
EOF

    # Cấu hình hostapd cho uap0
    cat << EOF > /etc/hostapd/hostapd.conf
interface=uap0
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

    # Trỏ cấu hình DAEMON_CONF
    sed -i 's|#DAEMON_CONF=""|DAEMON_CONF="/etc/hostapd/hostapd.conf"|' /etc/default/hostapd 2>/dev/null || true

    systemctl unmask hostapd 2>/dev/null || true
    systemctl enable hostapd dnsmasq
    systemctl restart dhcpcd || true
    systemctl restart dnsmasq
    systemctl restart hostapd
fi

# 5. Hoàn tất và kiểm tra
echo "[5/5] Kiểm tra trạng thái các interface..."
echo "----------------------------------------------------------"
ip -br addr show wlan0 || true
ip -br addr show uap0 || true
echo "----------------------------------------------------------"

echo ""
echo "=========================================================="
echo "    ✓ CÀI ĐẶT DUAL WI-FI THÀNH CÔNG!                      "
echo "=========================================================="
echo "1. Card 'wlan0': Tiếp tục bắt Wi-Fi ngoài bình thường."
echo "2. Card 'uap0':  Đang phát sóng '$AP_SSID' (Pass: '$AP_PASS')."
echo "3. Bạn có thể mở điện thoại kết nối vào '$AP_SSID'"
echo "   và truy cập: http://$AP_IP:8080 bất cứ lúc nào!"
echo "=========================================================="
