#!/bin/bash
# Setup Cloudflare Tunnel for remote access to FLIRONE
set -e

echo "=== FLIRONE Remote Access Setup (Cloudflare Tunnel) ==="

# Check if cloudflared is installed
if ! command -v cloudflared &> /dev/null
then
    echo "cloudflared not found. Installing for ARM64..."
    wget -q https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64 -O cloudflared
    chmod +x cloudflared
    sudo mv cloudflared /usr/local/bin/
    echo "cloudflared installed successfully."
else
    echo "cloudflared is already installed."
fi

echo ""
echo "1. Login to Cloudflare. This will open a browser window."
echo "   Please authenticate with your Cloudflare account and authorize the domain."
cloudflared tunnel login

echo ""
read -p "2. Enter a name for your tunnel (e.g., flirone-tunnel): " TUNNEL_NAME
if [ -z "$TUNNEL_NAME" ]; then
    TUNNEL_NAME="flirone-tunnel"
fi

echo "Creating tunnel: $TUNNEL_NAME..."
cloudflared tunnel create "$TUNNEL_NAME"

echo ""
read -p "3. Enter the hostname you want to use (e.g., thermal.yourdomain.com): " HOSTNAME
if [ -z "$HOSTNAME" ]; then
    echo "Hostname is required. Aborting."
    exit 1
fi

echo "Routing DNS to tunnel..."
cloudflared tunnel route dns "$TUNNEL_NAME" "$HOSTNAME"

echo ""
echo "4. Creating configuration file..."
CRED_FILE=$(ls ~/.cloudflared/*.json | head -n 1)
if [ -z "$CRED_FILE" ]; then
    echo "Error: Credentials file not found!"
    exit 1
fi

cat << EOF > ~/.cloudflared/config.yml
tunnel: $TUNNEL_NAME
credentials-file: $CRED_FILE

ingress:
  - hostname: $HOSTNAME
    service: http://localhost:8080
  - service: http_status:404
EOF

echo ""
echo "5. Installing cloudflared as a systemd service..."
sudo cloudflared service install
sudo cp ~/.cloudflared/config.yml /etc/cloudflared/
sudo systemctl start cloudflared
sudo systemctl enable cloudflared

echo ""
echo "Creating a run script that starts both flirone and the tunnel..."
cat << 'EOF' > start_flirone_remote.sh
#!/bin/bash
set -e
cd "$(dirname "$0")"

echo "Checking cloudflared service..."
if ! systemctl is-active --quiet cloudflared; then
    echo "Starting cloudflared tunnel..."
    sudo systemctl start cloudflared
fi

echo "Starting FLIRONE v2..."
CONFIG="flirone_config.json"
sudo ./flirone "$CONFIG" 2>&1 | tee -a flirone.log
EOF

chmod +x start_flirone_remote.sh

echo ""
echo "=== Setup Complete ==="
echo "Your FLIRONE will be accessible at: https://$HOSTNAME"
echo "To run the camera with remote access enabled, use: ./start_flirone_remote.sh"
