const stateFields = {
  model: document.getElementById("model"),
  alsa_input: document.getElementById("alsa_input"),
  alsa_output: document.getElementById("alsa_output"),
  sample_rate: document.getElementById("sample_rate"),
  input_gain_db: document.getElementById("input_gain_db"),
  output_gain_db: document.getElementById("output_gain_db"),
  bass_db: document.getElementById("bass_db"),
  mid_db: document.getElementById("mid_db"),
  treble_db: document.getElementById("treble_db"),
  tone_stack: document.getElementById("tone_stack"),
  tone_position: document.getElementById("tone_position"),
};

const rangeOutputs = {
  input_gain_db: document.getElementById("input_gain_db_value"),
  output_gain_db: document.getElementById("output_gain_db_value"),
  bass_db: document.getElementById("bass_db_value"),
  mid_db: document.getElementById("mid_db_value"),
  treble_db: document.getElementById("treble_db_value"),
};

const statusMessage = document.getElementById("status-message");
const healthPill = document.getElementById("health-pill");
const form = document.getElementById("control-form");
const refreshButton = document.getElementById("refresh-button");

function setStatus(message, isError = false) {
  statusMessage.textContent = message;
  statusMessage.dataset.error = isError ? "true" : "false";
}

function setHealth(ok) {
  healthPill.textContent = ok ? "Online" : "Offline";
  healthPill.dataset.ok = ok ? "true" : "false";
}

function updateRangeOutputs() {
  Object.entries(rangeOutputs).forEach(([key, output]) => {
    output.textContent = `${Number(stateFields[key].value).toFixed(1)} dB`;
  });
}

function populateModels(models, selected) {
  const select = stateFields.model;
  select.innerHTML = "";

  const placeholder = document.createElement("option");
  placeholder.value = "";
  placeholder.textContent = models.length ? "Select a model" : "No models found";
  select.appendChild(placeholder);

  models.forEach((modelName) => {
    const option = document.createElement("option");
    option.value = modelName;
    option.textContent = modelName;
    if (modelName === selected) {
      option.selected = true;
    }
    select.appendChild(option);
  });

  if (!selected) {
    select.value = "";
  }
}

function applyState(state) {
  stateFields.model.value = state.model || "";
  stateFields.alsa_input.value = state.audio.alsa_input;
  stateFields.alsa_output.value = state.audio.alsa_output;
  stateFields.sample_rate.value = state.audio.sample_rate;
  stateFields.input_gain_db.value = state.params.input_gain_db;
  stateFields.output_gain_db.value = state.params.output_gain_db;
  stateFields.bass_db.value = state.params.bass_db;
  stateFields.mid_db.value = state.params.mid_db;
  stateFields.treble_db.value = state.params.treble_db;
  stateFields.tone_stack.value = state.params.tone_stack;
  stateFields.tone_position.value = state.params.tone_position;
  updateRangeOutputs();
}

function collectState() {
  return {
    model: stateFields.model.value,
    audio: {
      alsa_input: stateFields.alsa_input.value.trim(),
      alsa_output: stateFields.alsa_output.value.trim(),
      sample_rate: Number.parseInt(stateFields.sample_rate.value, 10),
    },
    params: {
      input_gain_db: Number.parseFloat(stateFields.input_gain_db.value),
      output_gain_db: Number.parseFloat(stateFields.output_gain_db.value),
      bass_db: Number.parseFloat(stateFields.bass_db.value),
      mid_db: Number.parseFloat(stateFields.mid_db.value),
      treble_db: Number.parseFloat(stateFields.treble_db.value),
      tone_stack: stateFields.tone_stack.value,
      tone_position: stateFields.tone_position.value,
    },
  };
}

async function fetchJson(url, options = {}) {
  const response = await fetch(url, {
    headers: {
      "Content-Type": "application/json",
    },
    ...options,
  });

  const payload = await response.json();
  if (!response.ok) {
    const detail = payload.errors ? payload.errors.join(", ") : "Request failed";
    throw new Error(detail);
  }
  return payload;
}

async function refreshState() {
  setStatus("Loading current config...");
  try {
    const [health, models, state] = await Promise.all([
      fetchJson("/api/health"),
      fetchJson("/api/models"),
      fetchJson("/api/state"),
    ]);
    setHealth(Boolean(health.ok));
    populateModels(models.models, state.model);
    applyState(state);
    setStatus("Loaded current config.");
  } catch (error) {
    setHealth(false);
    setStatus(error.message, true);
  }
}

async function saveState(event) {
  event.preventDefault();
  setStatus("Saving config...");
  try {
    const payload = collectState();
    const response = await fetchJson("/api/state", {
      method: "POST",
      body: JSON.stringify(payload),
    });
    applyState(response.state);
    setStatus("Config saved. Restart-free parameter changes will take effect once the engine watches config.json.");
  } catch (error) {
    setStatus(error.message, true);
  }
}

Object.values(stateFields).forEach((field) => {
  if (field.type === "range") {
    field.addEventListener("input", updateRangeOutputs);
  }
});

form.addEventListener("submit", saveState);
refreshButton.addEventListener("click", refreshState);

refreshState();
