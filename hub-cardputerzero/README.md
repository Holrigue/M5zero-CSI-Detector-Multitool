# hub-cardputerzero — the RuView sensing-server host

Under the RuView-based direction (see [`../research/prior-art-ruview.md`](../research/prior-art-ruview.md)),
the CardputerZero's job is clear and plays to its strength (Linux compute): it
**runs the RuView sensing-server** — ingesting CSI from the ESP32-S3 node and
serving the REST + WebSocket API the Tab5 dashboard consumes.

```
ESP32-S3 CSI node ──UDP CSI :5005──▶  CardputerZero (this) : RuView sensing-server
                                        REST :3000  ·  WS :3001 /ws/sensing
                                                     │
                                                     ▼  over the kit's local WiFi
                                             Tab5 RuView dashboard client
```

## Run the server

```bash
./install-ruview.sh          # Docker (recommended); arm64 image runs on the CM0
# options: HTTP_PORT / WS_PORT / CSI_PORT, RUVIEW_API_TOKEN=<secret> for bearer auth
```

`install-ruview.sh` pulls `ruvnet/wifi-densepose`, runs it with the right ports
(`3000` REST, `3001` WS, `5005/udp` CSI), waits for `/api/v1/status`, and prints
the exact `RUVIEW_HOST` / `WS` values to put in the Tab5's `credentials.ini`.

**Dev with no hardware:** run the same script on any arm64/amd64 Linux box — the
RuView image serves the full API on **simulated data**, so the Tab5 client is
fully developable before any node or CardputerZero exists.

## Provision the ESP32-S3 CSI node (RuView firmware)

Flash RuView's `esp32-csi-node` firmware (browser flasher or esptool), then point
it at this server:

```bash
python firmware/esp32-csi-node/provision.py --port <PORT> \
  --ssid "<kit-wifi>" --password "<pass>" \
  --target-ip <this-host-ip> --target-port 5005
```

(from the [RuView repo](https://github.com/ruvnet/ruview); writes the `csi_cfg`
NVS at `0x9000`). Confirm frames climb via `GET /api/v1/nodes`.

## To verify on the real CardputerZero (on receipt)

- **arch/kernel:** `uname -m` (expect `aarch64`) and `uname -r`. Docker image is
  multi-arch (arm64), so it should run as-is.
- **CM0 horsepower (honest caveat):** the light path (presence / motion / vitals,
  ~8 KB model) is benchmarked to run on a Pi; heavy pose / world-model are Pi 5 /
  GPU territory. On the CM0 (Pi Zero 2W-class, 512 MB) expect the light path to be
  fine — measure it; disable heavy models if needed.
- **LAN reachability:** the API binds all interfaces (so the Tab5 can reach it) —
  set `RUVIEW_API_TOKEN` if you don't want it open on the kit network.
- **UART mux (`G4_I2C/UART_SW`)** is irrelevant to this path — the node→server
  link is UDP over WiFi, not UART.

## Non-Docker alternative

Build the RuView `wifi-densepose-sensing-server` crate from source (Rust) and run
it under systemd — Ragnar's `scripts/install_sensing.sh` is a working reference.
Only needed if you'd rather not use Docker on the CM0.
