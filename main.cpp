// main.cpp
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <portaudio.h>
#ifdef __linux__
#include <pa_linux_alsa.h>
#endif

#include "NAM/get_dsp.h"

namespace {
constexpr unsigned long kFramesPerBuffer = 128;
constexpr unsigned long kMaxBlockSize = 1024;
constexpr double kFallbackSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kInputChannels = 1;
constexpr int kOutputChannels = 2;

std::atomic<bool> g_running{true};

enum class ToneStackType {
  kPostEq,
  kNone,
  kFender,
  kMarshall,
  kVox,
};

enum class TonePosition {
  kPost,
  kPre,
};

void OnSignal(int) { g_running = false; }

void PrintUsage(const char* programName) {
  std::cerr << "usage: " << programName
            << " /path/to/model.nam [device-name]"
            << " [--input-gain-db DB] [--output-gain-db DB]"
            << " [--bass-db DB] [--mid-db DB] [--treble-db DB]"
            << " [--tone-stack NAME] [--tone-position pre|post]"
            << " [--alsa-device NAME] [--alsa-input NAME] [--alsa-output NAME]\n";
  std::cerr << "example: " << programName
            << " model.nam pisound"
            << " --input-gain-db 6 --tone-stack fender --tone-position pre"
            << " --bass-db 3 --mid-db -2 --treble-db 2 --output-gain-db -3\n";
  std::cerr << "Tone stacks: none, post-eq (default), fender, marshall, vox\n";
  std::cerr << "ALSA example: " << programName
            << " model.nam --alsa-device plughw:2,0\n";
}

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool ParseFlagValue(const std::string& arg,
                    const std::string& flag,
                    int& index,
                    int argc,
                    char** argv,
                    double& valueOut) {
  const std::string prefix = flag + "=";
  try {
    if (arg == flag) {
      if (index + 1 >= argc) {
        std::cerr << "Missing value for " << flag << "\n";
        return false;
      }
      valueOut = std::stod(argv[++index]);
      return true;
    }

    if (arg.rfind(prefix, 0) == 0) {
      valueOut = std::stod(arg.substr(prefix.size()));
      return true;
    }
  } catch (const std::exception&) {
    std::cerr << "Invalid numeric value for " << flag << "\n";
    return false;
  }

  return false;
}

bool ParseStringFlagValue(const std::string& arg,
                          const std::string& flag,
                          int& index,
                          int argc,
                          char** argv,
                          std::string& valueOut) {
  const std::string prefix = flag + "=";
  if (arg == flag) {
    if (index + 1 >= argc) {
      std::cerr << "Missing value for " << flag << "\n";
      return false;
    }
    valueOut = argv[++index];
    return true;
  }

  if (arg.rfind(prefix, 0) == 0) {
    valueOut = arg.substr(prefix.size());
    return true;
  }

  return false;
}

NAM_SAMPLE DbToLinear(double db) {
  return static_cast<NAM_SAMPLE>(std::pow(10.0, db / 20.0));
}

const char* ToneStackTypeToString(ToneStackType type) {
  switch (type) {
    case ToneStackType::kNone:
      return "none";
    case ToneStackType::kFender:
      return "fender";
    case ToneStackType::kMarshall:
      return "marshall";
    case ToneStackType::kVox:
      return "vox";
    case ToneStackType::kPostEq:
    default:
      return "post-eq";
  }
}

const char* TonePositionToString(TonePosition position) {
  switch (position) {
    case TonePosition::kPre:
      return "pre";
    case TonePosition::kPost:
    default:
      return "post";
  }
}

bool ParseToneStackType(const std::string& value, ToneStackType& typeOut) {
  const std::string lowered = ToLower(value);
  if (lowered == "post-eq" || lowered == "eq" || lowered == "default") {
    typeOut = ToneStackType::kPostEq;
    return true;
  }
  if (lowered == "none" || lowered == "off") {
    typeOut = ToneStackType::kNone;
    return true;
  }
  if (lowered == "fender") {
    typeOut = ToneStackType::kFender;
    return true;
  }
  if (lowered == "marshall") {
    typeOut = ToneStackType::kMarshall;
    return true;
  }
  if (lowered == "vox") {
    typeOut = ToneStackType::kVox;
    return true;
  }
  return false;
}

bool ParseTonePosition(const std::string& value, TonePosition& positionOut) {
  const std::string lowered = ToLower(value);
  if (lowered == "post") {
    positionOut = TonePosition::kPost;
    return true;
  }
  if (lowered == "pre") {
    positionOut = TonePosition::kPre;
    return true;
  }
  return false;
}

struct Biquad {
  double b0 = 1.0;
  double b1 = 0.0;
  double b2 = 0.0;
  double a1 = 0.0;
  double a2 = 0.0;
  double z1 = 0.0;
  double z2 = 0.0;

  void Reset() {
    z1 = 0.0;
    z2 = 0.0;
  }

  NAM_SAMPLE Process(NAM_SAMPLE input) {
    const double x = static_cast<double>(input);
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return static_cast<NAM_SAMPLE>(y);
  }

  void ConfigureLowShelf(double sampleRate, double frequencyHz, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequencyHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double sqrtA = std::sqrt(A);
    const double alpha = sinw0 / std::sqrt(2.0);
    const double beta = 2.0 * sqrtA * alpha;

    const double rawB0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + beta);
    const double rawB1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
    const double rawB2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - beta);
    const double rawA0 = (A + 1.0) + (A - 1.0) * cosw0 + beta;
    const double rawA1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
    const double rawA2 = (A + 1.0) + (A - 1.0) * cosw0 - beta;

    b0 = rawB0 / rawA0;
    b1 = rawB1 / rawA0;
    b2 = rawB2 / rawA0;
    a1 = rawA1 / rawA0;
    a2 = rawA2 / rawA0;
  }

  void ConfigurePeaking(double sampleRate, double frequencyHz, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequencyHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * q);

    const double rawB0 = 1.0 + alpha * A;
    const double rawB1 = -2.0 * cosw0;
    const double rawB2 = 1.0 - alpha * A;
    const double rawA0 = 1.0 + alpha / A;
    const double rawA1 = -2.0 * cosw0;
    const double rawA2 = 1.0 - alpha / A;

    b0 = rawB0 / rawA0;
    b1 = rawB1 / rawA0;
    b2 = rawB2 / rawA0;
    a1 = rawA1 / rawA0;
    a2 = rawA2 / rawA0;
  }

  void ConfigureHighShelf(double sampleRate, double frequencyHz, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * frequencyHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double sqrtA = std::sqrt(A);
    const double alpha = sinw0 / std::sqrt(2.0);
    const double beta = 2.0 * sqrtA * alpha;

    const double rawB0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + beta);
    const double rawB1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
    const double rawB2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - beta);
    const double rawA0 = (A + 1.0) - (A - 1.0) * cosw0 + beta;
    const double rawA1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
    const double rawA2 = (A + 1.0) - (A - 1.0) * cosw0 - beta;

    b0 = rawB0 / rawA0;
    b1 = rawB1 / rawA0;
    b2 = rawB2 / rawA0;
    a1 = rawA1 / rawA0;
    a2 = rawA2 / rawA0;
  }
};

struct ToneControls {
  ToneStackType type = ToneStackType::kPostEq;
  Biquad bass;
  Biquad mid;
  Biquad treble;

  void Configure(ToneStackType newType,
                 double sampleRate,
                 double bassDb,
                 double midDb,
                 double trebleDb) {
    type = newType;
    switch (type) {
      case ToneStackType::kNone:
        bass = {};
        mid = {};
        treble = {};
        break;
      case ToneStackType::kFender:
        // This is a Fender-style voicing approximation, not an exact passive circuit solve.
        bass.ConfigureLowShelf(sampleRate, 90.0, bassDb);
        mid.ConfigurePeaking(sampleRate, 500.0, 0.55, midDb);
        treble.ConfigureHighShelf(sampleRate, 2200.0, trebleDb);
        break;
      case ToneStackType::kMarshall:
        // Marshall-style voicing approximation with stronger upper-mid emphasis.
        bass.ConfigureLowShelf(sampleRate, 110.0, bassDb);
        mid.ConfigurePeaking(sampleRate, 850.0, 1.05, midDb);
        treble.ConfigureHighShelf(sampleRate, 3200.0, trebleDb);
        break;
      case ToneStackType::kVox:
        // Vox-style voicing approximation with lower bass shelf and more presence.
        bass.ConfigureLowShelf(sampleRate, 75.0, bassDb);
        mid.ConfigurePeaking(sampleRate, 650.0, 0.7, midDb);
        treble.ConfigureHighShelf(sampleRate, 4200.0, trebleDb);
        break;
      case ToneStackType::kPostEq:
      default:
        bass.ConfigureLowShelf(sampleRate, 120.0, bassDb);
        mid.ConfigurePeaking(sampleRate, 750.0, 0.8, midDb);
        treble.ConfigureHighShelf(sampleRate, 3500.0, trebleDb);
        break;
    }
    Reset();
  }

  void Reset() {
    bass.Reset();
    mid.Reset();
    treble.Reset();
  }

  NAM_SAMPLE Process(NAM_SAMPLE input) {
    if (type == ToneStackType::kNone) {
      return input;
    }
    return treble.Process(mid.Process(bass.Process(input)));
  }
};

void CheckPa(PaError err, const char* what) {
  if (err != paNoError) {
    std::cerr << what << ": " << Pa_GetErrorText(err) << "\n";
    std::exit(1);
  }
}

void PrintDevices() {
  const int count = Pa_GetDeviceCount();
  for (int i = 0; i < count; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (!info) continue;
    std::cerr << "[" << i << "] " << info->name
              << " in=" << info->maxInputChannels
              << " out=" << info->maxOutputChannels << "\n";
  }
}

PaDeviceIndex FindDuplexDevice(const std::string& nameNeedle) {
  const std::string needle = ToLower(nameNeedle);
  const int count = Pa_GetDeviceCount();

  for (int i = 0; i < count; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (!info) continue;

    const std::string name = ToLower(info->name ? info->name : "");
    const bool matches = name.find(needle) != std::string::npos;
    const bool duplexOk =
        info->maxInputChannels >= kInputChannels &&
        info->maxOutputChannels >= kOutputChannels;

    if (matches && duplexOk) {
      return i;
    }
  }

  return paNoDevice;
}

struct AppState {
  std::unique_ptr<nam::DSP> model;
  std::vector<NAM_SAMPLE> inputMono;
  std::vector<NAM_SAMPLE> outputMono;
  NAM_SAMPLE inputGainLinear = static_cast<NAM_SAMPLE>(1.0);
  NAM_SAMPLE outputGainLinear = static_cast<NAM_SAMPLE>(1.0);
  ToneControls toneControls;
  TonePosition tonePosition = TonePosition::kPost;
};

int AudioCallback(const void* inputBuffer,
                  void* outputBuffer,
                  unsigned long framesPerBuffer,
                  const PaStreamCallbackTimeInfo*,
                  PaStreamCallbackFlags,
                  void* userData) {
  auto* state = static_cast<AppState*>(userData);
  auto* out = static_cast<float*>(outputBuffer);
  const auto* in = static_cast<const float*>(inputBuffer);

  if (framesPerBuffer > state->inputMono.size()) {
    std::fill(out, out + framesPerBuffer * kOutputChannels, 0.0f);
    return g_running ? paContinue : paComplete;
  }

  for (unsigned long i = 0; i < framesPerBuffer; ++i) {
    const NAM_SAMPLE sample =
        in ? static_cast<NAM_SAMPLE>(in[i]) : static_cast<NAM_SAMPLE>(0);
    NAM_SAMPLE processed = sample * state->inputGainLinear;
    if (state->tonePosition == TonePosition::kPre) {
      processed = state->toneControls.Process(processed);
    }
    state->inputMono[i] = processed;
  }

  NAM_SAMPLE* inputs[] = {state->inputMono.data()};
  NAM_SAMPLE* outputs[] = {state->outputMono.data()};
  state->model->process(inputs, outputs, static_cast<int>(framesPerBuffer));

  for (unsigned long i = 0; i < framesPerBuffer; ++i) {
    NAM_SAMPLE toned = state->outputMono[i];
    if (state->tonePosition == TonePosition::kPost) {
      toned = state->toneControls.Process(toned);
    }
    const NAM_SAMPLE y = std::clamp(
        toned * state->outputGainLinear,
        static_cast<NAM_SAMPLE>(-1.0),
        static_cast<NAM_SAMPLE>(1.0));
    const float sample = static_cast<float>(y);
    out[i * 2 + 0] = sample;
    out[i * 2 + 1] = sample;
  }

  return g_running ? paContinue : paComplete;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const std::filesystem::path modelPath = argv[1];
  std::string deviceName = "pisound";
  std::string alsaInputDevice;
  std::string alsaOutputDevice;
  ToneStackType toneStackType = ToneStackType::kPostEq;
  TonePosition tonePosition = TonePosition::kPost;
  double inputGainDb = 0.0;
  double outputGainDb = 0.0;
  double bassDb = 0.0;
  double midDb = 0.0;
  double trebleDb = 0.0;

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    }

    if (ParseFlagValue(arg, "--input-gain-db", i, argc, argv, inputGainDb)) {
      continue;
    }

    if (ParseFlagValue(arg, "--output-gain-db", i, argc, argv, outputGainDb)) {
      continue;
    }

    if (ParseFlagValue(arg, "--bass-db", i, argc, argv, bassDb)) {
      continue;
    }

    if (ParseFlagValue(arg, "--mid-db", i, argc, argv, midDb)) {
      continue;
    }

    if (ParseFlagValue(arg, "--treble-db", i, argc, argv, trebleDb)) {
      continue;
    }

    std::string stringValue;
    if (ParseStringFlagValue(arg, "--tone-stack", i, argc, argv, stringValue)) {
      if (!ParseToneStackType(stringValue, toneStackType)) {
        std::cerr << "Unknown tone stack: " << stringValue << "\n";
        PrintUsage(argv[0]);
        return 1;
      }
      continue;
    }

    if (ParseStringFlagValue(arg, "--tone-position", i, argc, argv, stringValue)) {
      if (!ParseTonePosition(stringValue, tonePosition)) {
        std::cerr << "Unknown tone position: " << stringValue << "\n";
        PrintUsage(argv[0]);
        return 1;
      }
      continue;
    }

    if (ParseStringFlagValue(arg, "--alsa-device", i, argc, argv, alsaInputDevice)) {
      alsaOutputDevice = alsaInputDevice;
      continue;
    }

    if (ParseStringFlagValue(arg, "--alsa-input", i, argc, argv, alsaInputDevice)) {
      continue;
    }

    if (ParseStringFlagValue(arg, "--alsa-output", i, argc, argv, alsaOutputDevice)) {
      continue;
    }

    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << "\n";
      PrintUsage(argv[0]);
      return 1;
    }

    if (deviceName == "pisound") {
      deviceName = arg;
      continue;
    }

    std::cerr << "Unexpected positional argument: " << arg << "\n";
    PrintUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, OnSignal);

  AppState state;
  state.inputMono.resize(kMaxBlockSize);
  state.outputMono.resize(kMaxBlockSize);
  state.inputGainLinear = DbToLinear(inputGainDb);
  state.outputGainLinear = DbToLinear(outputGainDb);
  state.tonePosition = tonePosition;

  try {
    state.model = nam::get_dsp(modelPath);
  } catch (const std::exception& e) {
    std::cerr << "Failed to load model: " << e.what() << "\n";
    return 1;
  }

  if (!state.model) {
    std::cerr << "Failed to load model\n";
    return 1;
  }

  double sampleRate = state.model->GetExpectedSampleRate();
  if (sampleRate <= 0.0) {
    sampleRate = kFallbackSampleRate;
  }

  state.model->ResetAndPrewarm(sampleRate, static_cast<int>(kMaxBlockSize));
  state.toneControls.Configure(toneStackType, sampleRate, bassDb, midDb, trebleDb);

  CheckPa(Pa_Initialize(), "Pa_Initialize");

  PaStreamParameters inputParams{};
  PaStreamParameters outputParams{};
  inputParams.channelCount = kInputChannels;
  inputParams.sampleFormat = paFloat32;
  outputParams.channelCount = kOutputChannels;
  outputParams.sampleFormat = paFloat32;

  std::string selectedDeviceLabel;
#ifdef __linux__
  PaAlsaStreamInfo alsaInputInfo{};
  PaAlsaStreamInfo alsaOutputInfo{};
  const bool useExplicitAlsa =
      !alsaInputDevice.empty() || !alsaOutputDevice.empty();
  if (useExplicitAlsa) {
    if (alsaInputDevice.empty()) {
      alsaInputDevice = alsaOutputDevice;
    }
    if (alsaOutputDevice.empty()) {
      alsaOutputDevice = alsaInputDevice;
    }

    PaAlsa_InitializeStreamInfo(&alsaInputInfo);
    PaAlsa_InitializeStreamInfo(&alsaOutputInfo);
    alsaInputInfo.deviceString = alsaInputDevice.c_str();
    alsaOutputInfo.deviceString = alsaOutputDevice.c_str();

    inputParams.device = paUseHostApiSpecificDeviceSpecification;
    inputParams.suggestedLatency = 0.01;
    inputParams.hostApiSpecificStreamInfo = &alsaInputInfo;

    outputParams.device = paUseHostApiSpecificDeviceSpecification;
    outputParams.suggestedLatency = 0.01;
    outputParams.hostApiSpecificStreamInfo = &alsaOutputInfo;

    selectedDeviceLabel = "ALSA in=" + alsaInputDevice + " out=" + alsaOutputDevice;
  } else
#endif
  {
    const PaDeviceIndex device = FindDuplexDevice(deviceName);
    if (device == paNoDevice) {
      std::cerr << "Could not find a duplex audio device matching \"" << deviceName << "\".\n";
      std::cerr << "Available PortAudio devices:\n";
      PrintDevices();
      Pa_Terminate();
      return 1;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
    inputParams.device = device;
    inputParams.suggestedLatency = info->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;
    outputParams.device = device;
    outputParams.suggestedLatency = info->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;
    selectedDeviceLabel = info->name;
  }

  const PaError support =
      Pa_IsFormatSupported(&inputParams, &outputParams, sampleRate);
  if (support != paFormatIsSupported) {
    std::cerr << "Device \"" << selectedDeviceLabel << "\" does not support "
              << sampleRate << " Hz with 1 input / 2 output float32.\n";
    std::cerr << "You likely need a resampler or a different sample rate.\n";
    Pa_Terminate();
    return 1;
  }

  PaStream* stream = nullptr;
  CheckPa(
      Pa_OpenStream(&stream,
                    &inputParams,
                    &outputParams,
                    sampleRate,
                    kFramesPerBuffer,
                    paNoFlag,
                    AudioCallback,
                    &state),
      "Pa_OpenStream");
#ifdef __linux__
  if (!alsaInputDevice.empty() || !alsaOutputDevice.empty()) {
    PaAlsa_EnableRealtimeScheduling(stream, 1);
  }
#endif

  CheckPa(Pa_StartStream(stream), "Pa_StartStream");

  std::cout << "Running on " << selectedDeviceLabel
            << " at " << sampleRate
            << " Hz"
            << " (input gain " << inputGainDb << " dB"
            << ", tone stack " << ToneStackTypeToString(toneStackType)
            << " @ " << TonePositionToString(tonePosition)
            << ", bass " << bassDb << " dB"
            << ", mid " << midDb << " dB"
            << ", treble " << trebleDb << " dB"
            << ", output gain " << outputGainDb << " dB). Press Ctrl+C to quit.\n";

  while (g_running && Pa_IsStreamActive(stream) == 1) {
    Pa_Sleep(100);
  }

  CheckPa(Pa_StopStream(stream), "Pa_StopStream");
  CheckPa(Pa_CloseStream(stream), "Pa_CloseStream");
  CheckPa(Pa_Terminate(), "Pa_Terminate");
  return 0;
}
