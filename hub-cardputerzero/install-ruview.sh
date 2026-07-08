#!/usr/bin/env bash
# =============================================================================
# install-ruview.sh — run the RuView sensing-server on the CardputerZero.
#
# The CardputerZero (RPi CM0, arm64 Linux) is the brain: it runs RuView's
# sensing-server, which ingests CSI (UDP) from the ESP32-S3 node and exposes the
# REST + WebSocket API the Tab5 dashboard consumes.
#
# Also works on any arm64/amd64 Linux dev box — great for developing the Tab5
# client with no hardware, thanks to RuView's simulated-data mode.
#
# Usage:
#   ./install-ruview.sh                 # Docker (recommended), simulated data
#   HTTP_PORT=3000 WS_PORT=3001 CSI_PORT=5005 ./install-ruview.sh
#   RUVIEW_API_TOKEN=secret ./install-ruview.sh   # require bearer auth on the API
#
# Ports (RuView defaults): HTTP/REST 3000, WebSocket 3001 (/ws/sensing),
#                          UDP CSI ingest 5005.
# =============================================================================
set -euo pipefail

IMAGE="${IMAGE:-ruvnet/wifi-densepose:latest}"
NAME="${NAME:-ruview}"
HTTP_PORT="${HTTP_PORT:-3000}"
WS_PORT="${WS_PORT:-3001}"
CSI_PORT="${CSI_PORT:-5005}"

echo "== RuView sensing-server setup =="
echo "   arch: $(uname -m)   image: ${IMAGE}"

if ! command -v docker >/dev/null 2>&1; then
  cat <<'EOF'
Docker not found.

Option A (recommended): install Docker, then re-run this script.
  curl -fsSL https://get.docker.com | sh
  sudo usermod -aG docker "$USER"   # then log out/in

Option B: build the RuView sensing-server from source (Rust) — see
  https://github.com/ruvnet/ruview  (crate: wifi-densepose-sensing-server).
  Ragnar's scripts/install_sensing.sh is a working reference for a systemd unit.
EOF
  exit 1
fi

# Bind on all interfaces so the Tab5 can reach it over the kit's WiFi.
# NOTE: that exposes the API on the LAN — set RUVIEW_API_TOKEN to require auth.
AUTH_ARGS=()
if [[ -n "${RUVIEW_API_TOKEN:-}" ]]; then
  AUTH_ARGS=(-e "RUVIEW_API_TOKEN=${RUVIEW_API_TOKEN}")
  echo "   API bearer auth: ENABLED"
else
  echo "   API bearer auth: disabled (set RUVIEW_API_TOKEN to enable)"
fi

echo "-- pulling image --"
docker pull "${IMAGE}"

echo "-- (re)starting container '${NAME}' --"
docker rm -f "${NAME}" >/dev/null 2>&1 || true
docker run -d --name "${NAME}" --restart unless-stopped \
  -p "${HTTP_PORT}:3000" \
  -p "${WS_PORT}:3001" \
  -p "${CSI_PORT}:5005/udp" \
  "${AUTH_ARGS[@]}" \
  "${IMAGE}"

echo "-- waiting for the API --"
ok=0
for _ in $(seq 1 30); do
  if curl -fsS "http://127.0.0.1:${HTTP_PORT}/api/v1/status" >/dev/null 2>&1; then ok=1; break; fi
  sleep 1
done

IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
if [[ "${ok}" == "1" ]]; then
  echo "RuView is UP."
else
  echo "Container started but /api/v1/status didn't answer yet — check: docker logs ${NAME}"
fi
cat <<EOF

Point the Tab5 client at this box:
  RUVIEW_HOST = ${IP:-<this-host-ip>}
  RUVIEW_PORT = ${HTTP_PORT}        (REST)
  WS          = ws://${IP:-<this-host-ip>}:${WS_PORT}/ws/sensing
  CSI ingest  = UDP ${CSI_PORT}     (ESP32-S3 node target-ip/port)

  logs:    docker logs -f ${NAME}
  stop:    docker rm -f ${NAME}
  status:  curl http://127.0.0.1:${HTTP_PORT}/api/v1/status
EOF
