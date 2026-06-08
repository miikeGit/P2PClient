# P2P File Transfer

Desktop app for sending files directly between two machines over WebRTC. An MQTT broker is used only to exchange connection details (signaling) — the file data itself travels peer-to-peer and never passes through a server.

Built with Qt Widgets and C++20.

<p align="center">
<img width="609" height="814" alt="image" src="https://github.com/user-attachments/assets/60c20e8e-6661-4d72-8d19-712bc58c9db4" />
</p>

## How it works

Each instance generates a random ID on startup and connects to an MQTT broker. To start a transfer:

1. One peer enters the other's ID and connects.
2. The clients exchange SDP/ICE over MQTT and open a direct WebRTC connection.
3. Files are sent over two data channels — one for control messages (JSON), one for the raw binary chunks.

The sender streams the file in chunks and respects channel backpressure so it doesn't overrun the send buffer. After the last chunk it sends an XXH3-64 hash, which the receiver checks against the file it wrote. If a transfer is interrupted, it can resume from the partial file by hashing what's already on disk.

Only one transfer runs at a time. The transfer logic lives on its own worker thread so the UI stays responsive.

## Building

Needs Qt 6 (falls back to Qt 5), CMake 3.16+, and a C++20 compiler (MSVC on Windows).

mbedTLS, qmqtt, and libdatachannel are fetched automatically by CMake, so there's no manual dependency setup — the first configure will take a while.

```
cmake -B build
cmake --build build --config Debug
```

`config.json` is copied next to the executable after each build.

## Configuration

Settings live in `config.json`:

```json
{
  "mqtt": {
    "host": "broker.emqx.io",
    "port": 8883
  },
  "webrtc": {
    "ice_servers": [
      { "urls": "stun:stun.l.google.com:19302" },
      {
        "urls": "turn:openrelay.metered.ca:80",
        "username": "openrelayproject",
        "password": "openrelayproject"
      }
    ]
  }
}
```

- `mqtt` — broker used for signaling. The default is a public test broker; point it at your own for anything real.
- `webrtc.ice_servers` — STUN/TURN servers for NAT traversal. TURN is needed when both peers sit behind restrictive NATs.

The download folder is picked in the app and saved back to `config.json` under `settings.download_path`.

## Usage

1. Launch the app on both machines. Each shows its own ID once it reaches the broker.
2. Copy your ID and send it to the other person.
3. Paste their ID and click Connect.
4. Once connected, choose a file to send. The receiver gets it in their save location.

## Layout

- `p2pclient.*` — MQTT signaling and WebRTC connection handling
- `filetransfermanager.*` — chunked send/receive, backpressure, hashing, resume
- `appconfig.*` — reading and writing `config.json`
- `mainwindow.*` — UI
- `xxhash.*` — hashing
