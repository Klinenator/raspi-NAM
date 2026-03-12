from __future__ import annotations

import json
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Any

from flask import Flask, jsonify, request, send_from_directory


ROOT_DIR = Path(__file__).resolve().parents[1]
STATIC_DIR = ROOT_DIR / "web" / "static"
MODELS_DIR = ROOT_DIR / "models"
CONFIG_PATH = ROOT_DIR / "config.json"

DEFAULT_CONFIG: dict[str, Any] = {
    "model": "",
    "audio": {
        "alsa_input": "plughw:2,0",
        "alsa_output": "plughw:2,0",
        "sample_rate": 48000,
    },
    "params": {
        "input_gain_db": 0.0,
        "output_gain_db": 0.0,
        "bass_db": 0.0,
        "mid_db": 0.0,
        "treble_db": 0.0,
        "tone_stack": "post-eq",
        "tone_position": "post",
    },
}

PARAM_LIMITS = {
    "input_gain_db": (-24.0, 24.0),
    "output_gain_db": (-24.0, 24.0),
    "bass_db": (-12.0, 12.0),
    "mid_db": (-12.0, 12.0),
    "treble_db": (-12.0, 12.0),
}

TONE_STACKS = {"none", "post-eq", "fender", "marshall", "vox"}
TONE_POSITIONS = {"pre", "post"}

app = Flask(__name__, static_folder=str(STATIC_DIR), static_url_path="/static")


def deep_copy_config(config: dict[str, Any]) -> dict[str, Any]:
    return json.loads(json.dumps(config))


def ensure_config_exists() -> None:
    if CONFIG_PATH.exists():
        return
    atomic_write_json(DEFAULT_CONFIG)


def read_config() -> dict[str, Any]:
    ensure_config_exists()
    with CONFIG_PATH.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def atomic_write_json(payload: dict[str, Any]) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(
        "w",
        encoding="utf-8",
        dir=CONFIG_PATH.parent,
        delete=False,
    ) as temp_handle:
        json.dump(payload, temp_handle, indent=2)
        temp_handle.write("\n")
        temp_path = Path(temp_handle.name)
    temp_path.replace(CONFIG_PATH)


def list_models() -> list[str]:
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    return sorted(path.name for path in MODELS_DIR.glob("*.nam"))


def validate_config(candidate: dict[str, Any]) -> list[str]:
    errors: list[str] = []

    model = candidate.get("model", "")
    if model and model not in list_models():
        errors.append(f"Unknown model: {model}")

    audio = candidate.get("audio", {})
    if not isinstance(audio, dict):
        errors.append("audio must be an object")
    else:
        sample_rate = audio.get("sample_rate")
        if not isinstance(sample_rate, int) or sample_rate <= 0:
            errors.append("audio.sample_rate must be a positive integer")
        for key in ("alsa_input", "alsa_output"):
            value = audio.get(key)
            if not isinstance(value, str) or not value:
                errors.append(f"audio.{key} must be a non-empty string")

    params = candidate.get("params", {})
    if not isinstance(params, dict):
        errors.append("params must be an object")
    else:
        for key, (minimum, maximum) in PARAM_LIMITS.items():
            value = params.get(key)
            if not isinstance(value, (int, float)):
                errors.append(f"params.{key} must be numeric")
                continue
            if value < minimum or value > maximum:
                errors.append(f"params.{key} must be between {minimum} and {maximum}")

        tone_stack = params.get("tone_stack")
        if tone_stack not in TONE_STACKS:
            errors.append(f"params.tone_stack must be one of {sorted(TONE_STACKS)}")

        tone_position = params.get("tone_position")
        if tone_position not in TONE_POSITIONS:
            errors.append(f"params.tone_position must be one of {sorted(TONE_POSITIONS)}")

    return errors


def merge_state(current: dict[str, Any], patch: dict[str, Any]) -> dict[str, Any]:
    merged = deep_copy_config(current)
    for top_level_key in ("model",):
        if top_level_key in patch:
            merged[top_level_key] = patch[top_level_key]

    for section_key in ("audio", "params"):
        section_patch = patch.get(section_key)
        if section_patch is None:
            continue
        if not isinstance(section_patch, dict):
            raise ValueError(f"{section_key} must be an object")
        merged_section = merged.setdefault(section_key, {})
        for key, value in section_patch.items():
            merged_section[key] = value
    return merged


@app.get("/")
def index() -> Any:
    return send_from_directory(STATIC_DIR, "index.html")


@app.get("/api/health")
def health() -> Any:
    return jsonify({"ok": True})


@app.get("/api/models")
def models() -> Any:
    return jsonify({"models": list_models()})


@app.get("/api/state")
def get_state() -> Any:
    return jsonify(read_config())


@app.post("/api/state")
def update_state() -> Any:
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        return jsonify({"ok": False, "errors": ["Request body must be a JSON object"]}), 400

    try:
        merged = merge_state(read_config(), payload)
    except ValueError as exc:
        return jsonify({"ok": False, "errors": [str(exc)]}), 400

    errors = validate_config(merged)
    if errors:
        return jsonify({"ok": False, "errors": errors}), 400

    atomic_write_json(merged)
    return jsonify({"ok": True, "state": merged})


@app.post("/api/model")
def update_model() -> Any:
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict) or "model" not in payload:
        return jsonify({"ok": False, "errors": ["model is required"]}), 400

    merged = merge_state(read_config(), {"model": payload["model"]})
    errors = validate_config(merged)
    if errors:
        return jsonify({"ok": False, "errors": errors}), 400

    atomic_write_json(merged)
    return jsonify({"ok": True, "state": merged})


@app.get("/static/<path:asset_path>")
def static_files(asset_path: str) -> Any:
    return send_from_directory(STATIC_DIR, asset_path)


if __name__ == "__main__":
    ensure_config_exists()
    app.run(host="0.0.0.0", port=8080, debug=True)
