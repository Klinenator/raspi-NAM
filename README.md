# raspi-NAM

`raspi-NAM` is a small Raspberry Pi host for [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore), aimed at live use with Pisound.

Current focus:
- load a `.nam` model on a Raspberry Pi
- run it through Pisound-oriented ALSA audio I/O
- shape it with host-side gain and tone controls
- expose a local web UI for config editing

## Status

This repo is an MVP prototype.

Working now:
- native C++ audio host
- explicit ALSA device targeting for Pisound-friendly setups
- input and output gain
- tone-stack options: `post-eq`, `none`, `fender`, `marshall`, `vox`
- tone position: `pre` or `post`
- local Flask UI that edits `config.json`

Still in progress:
- live config reload in the audio engine
- hot model switching while audio is running
- exact passive-circuit tone stack modeling
- preset management

## Project layout

```text
.
├── config.json
├── main.cpp
├── models/
└── web/
    ├── requirements.txt
    ├── server.py
    └── static/
        ├── app.js
        ├── index.html
        └── styles.css
```

## Requirements

For the audio host:
- Raspberry Pi OS 64-bit recommended
- a local checkout of `NeuralAmpModelerCore`
- C++20 compiler
- CMake
- PortAudio
- nlohmann json headers

For the web UI:
- Python 3
- Flask

## Audio host

`main.cpp` is the standalone host entry point.

### Fresh Bookworm setup

On a fresh Raspberry Pi OS Bookworm 64-bit install:

```bash
sudo apt update
sudo apt full-upgrade -y
sudo apt install -y git cmake build-essential clang pkg-config \
  portaudio19-dev nlohmann-json3-dev python3 python3-venv python3-pip
```

Install the Pisound software stack, then reboot:

```bash
curl https://blokas.io/pisound/install.sh | sh
sudo reboot
```

Clone both repositories side-by-side:

```bash
git clone https://github.com/Klinenator/raspi-NAM.git
git clone --recursive https://github.com/sdatkinson/NeuralAmpModelerCore.git
```

If you forgot `--recursive`, fix the NAM core checkout with:

```bash
git -C NeuralAmpModelerCore submodule update --init --recursive
```

Build the host:

```bash
cd raspi-NAM
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j"$(nproc)"
```

If `NeuralAmpModelerCore` is not cloned next to this repo, point CMake at it explicitly:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DNAM_CORE_DIR=/path/to/NeuralAmpModelerCore
```

The host currently supports:
- loading a `.nam` model from disk
- selecting either a named PortAudio device or explicit ALSA devices
- host-side gain controls
- host-side tone shaping

### Example run

```bash
./build/nam_pisound_host "My Model.nam" \
  --alsa-device plughw:2,0 \
  --input-gain-db 6 \
  --tone-stack fender \
  --tone-position pre \
  --bass-db 2 \
  --mid-db -2 \
  --treble-db 1 \
  --output-gain-db -3
```

### Tone stacks

- `post-eq`
  Default generic 3-band EQ.
- `none`
  No tone shaping.
- `fender`
  Fender-style voicing approximation.
- `marshall`
  Marshall-style voicing approximation with stronger upper mids.
- `vox`
  Vox-style voicing approximation with brighter presence.

These are currently voicing approximations, not exact passive circuit simulations.

## Models

The web UI looks for models in:

[`models/`](/Users/seankline/src/raspi-NAM/models)

Only files ending in `.nam` are listed.

## Web UI

The Flask server serves a small local control page and reads/writes:

[`config.json`](/Users/seankline/src/raspi-NAM/config.json)

Start it with:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r web/requirements.txt
python3 web/server.py
```

Then open:

- `http://localhost:8080`
- or `http://<pi-ip>:8080` from another device on the network

### Current UI behavior

The UI can:
- list models from `models/`
- edit audio and tone parameters
- save the current state back to `config.json`

Important:
- the UI currently updates config on disk
- the audio engine does not yet watch `config.json` live
- for now, treat the web UI as configuration editing rather than true live control

## Next steps

The most important next engineering step is to refactor the audio host so it watches `config.json` and applies safe runtime parameter updates without restarting the process.

After that:
- live UI control becomes real
- model selection can be upgraded toward hot reload
- presets and MIDI/footswitch control become much easier to add
