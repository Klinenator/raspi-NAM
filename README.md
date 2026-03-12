# raspi-NAM

Small Raspberry Pi host for NeuralAmpModeler with Pisound-oriented audio I/O and a local web control UI.

## Current pieces

- Native C++ audio host using `NeuralAmpModelerCore`
- Pisound-friendly ALSA device selection
- Input/output gain controls
- Selectable tone stacks: `post-eq`, `none`, `fender`, `marshall`, `vox`
- Local Flask web UI skeleton for config editing

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

## Web UI

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r web/requirements.txt
python3 web/server.py
```

Then open `http://localhost:8080`.

## Notes

- The current web UI writes `config.json`; the audio engine still needs runtime config watching to apply those settings live without restart.
- Tone stacks are currently voicing approximations, not exact passive circuit simulations.
