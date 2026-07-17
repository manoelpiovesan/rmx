#include "app/App.hpp"
#include "core/Logger.hpp"
#include "samples/SampleLoader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <thread>

namespace xpad::app {

namespace {

bool isSupportedAudioFile(const std::filesystem::path& p) {
    if (!p.has_extension()) return false;
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return ext == ".wav" || ext == ".flac" || ext == ".mp3" || ext == ".ogg";
}

xpad::audio::QuantizeDivision toQuantDivisionFromRulerIndex(int idx) {
    switch (idx) {
        case 0: return xpad::audio::QuantizeDivision::Eighth;
        case 1: return xpad::audio::QuantizeDivision::Quarter;
        case 2: return xpad::audio::QuantizeDivision::Half;
        case 3: return xpad::audio::QuantizeDivision::Whole;
        case 4: return xpad::audio::QuantizeDivision::DoubleWhole;
        default: return xpad::audio::QuantizeDivision::Quarter;
    }
}

bool messageMatchesBinding(const xpad::midi::MidiMessage& msg,
                          const xpad::config::MidiLearnBinding& b) {
    if (b.number < 0) return false;
    if (b.channel >= 0 && static_cast<int>(msg.channel()) != b.channel) return false;

    if (b.messageType == "note") {
        return msg.type() == 0x90 && static_cast<int>(msg.data1) == b.number && msg.data2 > 0;
    }
    if (b.messageType == "cc") {
        return msg.type() == 0xB0 && static_cast<int>(msg.data1) == b.number;
    }
    return false;
}

std::string bindingToLabel(const xpad::config::MidiLearnBinding& b) {
    if (b.number < 0) return "-";
    const std::string type = (b.messageType == "cc") ? "CC" : "NOTE";
    if (b.channel >= 0) {
        return type + " " + std::to_string(b.number) + " ch" + std::to_string(b.channel + 1);
    }
    return type + " " + std::to_string(b.number);
}

} // namespace

App::App() : App(Config{}) {}

App::App(Config config) : cfg_(config) {
    if (!cfg_.configFile.empty()) {
        xCfg_.load(cfg_.configFile);
    } else {
        xCfg_.load(xpad::config::XPadConfig::defaultPath());
    }
    xCfg_.initialTempoBpm = cfg_.initialTempoBpm;
    xCfg_.quantum         = cfg_.quantum;
}

App::~App() {
    shutdownSubsystems();
}

int App::run() {
    initSubsystems();
    if (cfg_.headless) {
        runHeadless();
    } else {
        runGui();
    }
    shutdownSubsystems();
    return 0;
}

void App::initSubsystems() {
    core::Logger::info("X-1000 — inicializando...");

    linkManager_ = std::make_shared<xpad::link::LinkManager>(
        xpad::link::LinkManager::Config{
            .initialTempoBpm   = xCfg_.initialTempoBpm,
            .quantum           = xCfg_.quantum,
            .preferAbletonLink = cfg_.preferAbletonLink,
        });
    linkManager_->start();
    core::Logger::info(std::string("Link: ") + linkManager_->modeName());

    bank_ = std::make_shared<xpad::samples::SampleBank>();
    bank_->name = xCfg_.activeBankName;
    loadBank();

    scheduler_ = std::make_shared<xpad::audio::AudioScheduler>(bank_);
    scheduler_->setMasterVolume(xCfg_.masterVolume);
    scheduler_->setPitchSemitones(xCfg_.pitchSemitones);
    scheduler_->setFilterAmount(xCfg_.filterAmount);

    audioEngine_ = std::make_shared<xpad::audio::AudioEngine>(
        xpad::audio::AudioConfig{
            .deviceName       = xCfg_.audioDevice,
            .sampleRate       = xCfg_.sampleRate,
            .bufferSizeFrames = xCfg_.bufferSizeFrames,
        });
    audioEngine_->setScheduler(scheduler_);
    audioEngine_->setLinkStateProvider([this]() -> std::pair<double, double> {
        const auto snap = linkManager_->snapshot();
        return {snap.beat, snap.tempoBpm};
    });
    audioEngine_->start();
    xCfg_.audioDevice = audioEngine_->config().deviceName;

    midiManager_ = std::make_shared<xpad::midi::MidiManager>();
    midiManager_->setCallback([this](const xpad::midi::MidiMessage& msg) {
        onMidiMessage(msg);
    });

    const auto midiPorts = midiManager_->listPorts();
    if (midiPorts.empty()) {
        core::Logger::warn("MIDI: nenhuma porta encontrada");
    } else {
        std::string portsList;
        for (std::size_t i = 0; i < midiPorts.size(); ++i) {
            portsList += "[" + std::to_string(i) + "] " + midiPorts[i];
            if (i + 1 < midiPorts.size()) portsList += " | ";
        }
        core::Logger::info("MIDI ports detectadas: " + portsList);

        auto isVirtualPort = [](const std::string& name) {
            return name.find("Midi Through") != std::string::npos ||
                   name.find("LoopBe") != std::string::npos ||
                   name.find("Virtual") != std::string::npos;
        };

        bool opened = false;

        // 1) Try configured port first, unless it's the known virtual fallback.
        if (!xCfg_.midiPortName.empty() && !isVirtualPort(xCfg_.midiPortName)) {
            opened = midiManager_->openPortByName(xCfg_.midiPortName);
        }

        // 2) Prefer a real hardware port.
        if (!opened) {
            for (std::size_t i = 0; i < midiPorts.size(); ++i) {
                if (isVirtualPort(midiPorts[i])) continue;
                if (midiManager_->openPort(static_cast<std::uint32_t>(i))) {
                    xCfg_.midiPortName = midiPorts[i];
                    opened = true;
                    break;
                }
            }
        }

        // 3) Last resort: open first available (can be virtual).
        if (!opened && midiManager_->openPort(0)) {
            xCfg_.midiPortName = midiPorts[0];
            opened = true;
        }

        // 4) If still not open, retry by exact index scan (some drivers fail by cached name).
        if (!opened) {
            for (std::size_t i = 0; i < midiPorts.size(); ++i) {
                if (midiManager_->openPort(static_cast<std::uint32_t>(i))) {
                    xCfg_.midiPortName = midiPorts[i];
                    opened = true;
                    break;
                }
            }
        }

        if (!opened) {
            core::Logger::warn("MIDI: falha ao abrir qualquer porta (verifique se outro software nao esta segurando a controladora)");
        }
    }

    midiConnectedPortLabel_ = midiManager_->isOpen() ? midiManager_->openPortName() : "(none)";
    if (midiManager_->isOpen()) {
        xCfg_.midiPortName = midiManager_->openPortName();
    }

    rebuildMidiBindingCache();
}

void App::shutdownSubsystems() {
    xCfg_.save(xpad::config::XPadConfig::defaultPath());

    if (window_) { window_->shutdown(); window_.reset(); }
    if (audioEngine_) audioEngine_->stop();
    if (midiManager_) midiManager_->closePort();
    if (linkManager_) linkManager_->stop();
}

void App::loadBank() {
    availableSamplePaths_.clear();
    availableSampleNames_.clear();

    if (!std::filesystem::exists(xCfg_.samplesDirectory)) {
        core::Logger::warn("SampleBank: diretorio nao encontrado: " + xCfg_.samplesDirectory);
        std::filesystem::create_directories(xCfg_.samplesDirectory);
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(xCfg_.samplesDirectory)) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        if (!isSupportedAudioFile(path)) continue;

        availableSamplePaths_.push_back(path.string());
        availableSampleNames_.push_back(path.filename().string());
    }

    std::vector<std::size_t> order(availableSamplePaths_.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        return availableSampleNames_[a] < availableSampleNames_[b];
    });

    std::vector<std::string> sortedPaths;
    std::vector<std::string> sortedNames;
    sortedPaths.reserve(order.size());
    sortedNames.reserve(order.size());
    for (std::size_t idx : order) {
        sortedPaths.push_back(availableSamplePaths_[idx]);
        sortedNames.push_back(availableSampleNames_[idx]);
    }
    availableSamplePaths_ = std::move(sortedPaths);
    availableSampleNames_ = std::move(sortedNames);

    if (availableSamplePaths_.empty()) {
        core::Logger::warn("SampleBank: nenhum sample encontrado em " + xCfg_.samplesDirectory);
        return;
    }

    selectedSampleIndex_ = 0;
    if (!xCfg_.selectedSamplePath.empty()) {
        for (std::size_t i = 0; i < availableSamplePaths_.size(); ++i) {
            if (availableSamplePaths_[i] == xCfg_.selectedSamplePath) {
                selectedSampleIndex_ = static_cast<int>(i);
                break;
            }
        }
    }

    loadSelectedSampleIntoPad0();
    core::Logger::info("SampleBank: " + std::to_string(availableSamplePaths_.size()) + " sample(s) disponivel(is)");
}

void App::loadSelectedSampleIntoPad0() {
    if (selectedSampleIndex_ < 0 || selectedSampleIndex_ >= static_cast<int>(availableSamplePaths_.size())) {
        return;
    }

    const std::string& path = availableSamplePaths_[static_cast<std::size_t>(selectedSampleIndex_)];
    xpad::samples::SampleMetadata meta;
    meta.name       = availableSampleNames_[static_cast<std::size_t>(selectedSampleIndex_)];
    meta.filePath   = path;
    meta.mode       = xpad::samples::PadMode::OneShot;
    meta.referenceBpm = 0.0;

    auto sample = xpad::samples::loadSample(path, meta, xCfg_.sampleRate);
    if (!sample) {
        core::Logger::error("Falha ao carregar sample selecionado: " + path);
        return;
    }

    bank_->setPad(0, sample);
    xCfg_.selectedSamplePath = path;
    core::Logger::info("Sample selecionado: " + meta.name);
}

void App::rebuildMidiBindingCache() {
    midiBindingByControl_.clear();
    midiBindingLabelByControl_.clear();
    for (const auto& b : xCfg_.midiLearnBindings) {
        if (b.controlId.empty()) continue;
        midiBindingByControl_[b.controlId] = b;
        midiBindingLabelByControl_[b.controlId] = bindingToLabel(b);
    }
}

void App::setMidiLearnMode(bool enabled) {
    midiLearnMode_ = enabled;
    if (!enabled) {
        pendingMidiLearnControl_.clear();
    }
}

void App::applyMidiPortSelection(const std::string& portName) {
    const std::string previous = xCfg_.midiPortName;
    xCfg_.midiPortName = portName;

    if (!midiManager_) return;

    midiManager_->closePort();
    bool opened = portName.empty();
    if (!portName.empty()) {
        opened = midiManager_->openPortByName(portName);
    }

    if (!opened) {
        core::Logger::warn("MIDI: nao foi possivel abrir a porta selecionada, restaurando a anterior");
        xCfg_.midiPortName = previous;
        if (!previous.empty()) {
            midiManager_->openPortByName(previous);
        }
    }

    midiConnectedPortLabel_ = midiManager_->isOpen() ? midiManager_->openPortName() : "(none)";
    if (midiManager_->isOpen()) {
        xCfg_.midiPortName = midiManager_->openPortName();
    }
    xCfg_.save(xpad::config::XPadConfig::defaultPath());
}

void App::applyAudioDeviceSelection(const std::string& deviceName) {
    const std::string previous = xCfg_.audioDevice;
    xCfg_.audioDevice = deviceName;

    if (!audioEngine_) return;

    const bool wasRunning = audioEngine_->running();
    if (wasRunning) audioEngine_->stop();

    const auto previousEngine = audioEngine_;
    audioEngine_ = std::make_shared<xpad::audio::AudioEngine>(
        xpad::audio::AudioConfig{
            .deviceName       = xCfg_.audioDevice,
            .sampleRate       = xCfg_.sampleRate,
            .bufferSizeFrames = xCfg_.bufferSizeFrames,
        });
    audioEngine_->setScheduler(scheduler_);
    audioEngine_->setLinkStateProvider([this]() -> std::pair<double, double> {
        const auto snap = linkManager_->snapshot();
        return {snap.beat, snap.tempoBpm};
    });

    if (!audioEngine_->start()) {
        core::Logger::warn("Audio: nao foi possivel abrir a saida selecionada, restaurando a anterior");
        xCfg_.audioDevice = previous;
        audioEngine_ = previousEngine;
        if (audioEngine_) {
            audioEngine_->setScheduler(scheduler_);
            audioEngine_->setLinkStateProvider([this]() -> std::pair<double, double> {
                const auto snap = linkManager_->snapshot();
                return {snap.beat, snap.tempoBpm};
            });
            if (wasRunning) audioEngine_->start();
        }
        return;
    }

    xCfg_.audioDevice = audioEngine_->config().deviceName;
    xCfg_.save(xpad::config::XPadConfig::defaultPath());
}

void App::beginMidiLearn(const std::string& controlId) {
    if (!midiLearnMode_) return;
    pendingMidiLearnControl_ = controlId;
    core::Logger::info("MIDI Learn: aguardando entrada para '" + controlId + "'");
}

void App::applyMidiControl(const std::string& controlId, const xpad::midi::MidiMessage& msg) {
    if (controlId == "trigger") {
        const float vol = msg.isCC() ? (static_cast<float>(msg.data2) / 127.0f)
                                     : std::max(0.05f, static_cast<float>(msg.data2) / 127.0f);
        onTrigger(std::max(0.05f, vol));
        return;
    }

    if (controlId.rfind("roll_", 0) == 0) {
        const int idx = std::stoi(controlId.substr(5));
        if (idx == activeRollButton_) selectRoll(-1, 1.0f);
        else selectRoll(idx, 1.0f);
        return;
    }

    if (controlId == "master" && msg.isCC()) {
        const float v = static_cast<float>(msg.data2) / 127.0f;
        xCfg_.masterVolume = v;
        if (scheduler_) scheduler_->setMasterVolume(v);
        return;
    }

    if (controlId == "pitch" && msg.isCC()) {
        const float t = (static_cast<float>(msg.data2) / 127.0f) * 24.0f - 12.0f;
        xCfg_.pitchSemitones = t;
        if (scheduler_) scheduler_->setPitchSemitones(t);
        return;
    }

    if (controlId == "filter" && msg.isCC()) {
        const float v = (static_cast<float>(msg.data2) / 127.0f) * 2.0f - 1.0f;
        xCfg_.filterAmount = v;
        if (scheduler_) scheduler_->setFilterAmount(v);
        return;
    }

    if (controlId == "sample_next") {
        if (availableSamplePaths_.empty()) return;
        selectedSampleIndex_ = (selectedSampleIndex_ + 1) % static_cast<int>(availableSamplePaths_.size());
        loadSelectedSampleIntoPad0();
        return;
    }

    if (controlId == "sample_prev") {
        if (availableSamplePaths_.empty()) return;
        selectedSampleIndex_ = (selectedSampleIndex_ - 1 + static_cast<int>(availableSamplePaths_.size()))
                             % static_cast<int>(availableSamplePaths_.size());
        loadSelectedSampleIntoPad0();
        return;
    }
}

void App::onMidiMessage(const xpad::midi::MidiMessage& msg) {
    {
        std::ostringstream midi;
        if (msg.isCC()) {
            midi << "CC " << static_cast<int>(msg.data1)
                 << " v=" << static_cast<int>(msg.data2)
                 << " ch" << (static_cast<int>(msg.channel()) + 1);
        } else if (msg.isNoteOn()) {
            midi << "NoteOn " << static_cast<int>(msg.data1)
                 << " v=" << static_cast<int>(msg.data2)
                 << " ch" << (static_cast<int>(msg.channel()) + 1);
        } else if (msg.isNoteOff()) {
            midi << "NoteOff " << static_cast<int>(msg.data1)
                 << " ch" << (static_cast<int>(msg.channel()) + 1);
        } else {
            midi << "MIDI status=0x" << std::hex << static_cast<int>(msg.status)
                 << std::dec << " d1=" << static_cast<int>(msg.data1)
                 << " d2=" << static_cast<int>(msg.data2);
        }
        midiLastMessageLabel_ = midi.str();
    }

    // Learn mode: click target in UI, then move/press hardware control.
    if (midiLearnMode_ && !pendingMidiLearnControl_.empty() && (msg.isNoteOn() || msg.isCC())) {
        xpad::config::MidiLearnBinding b;
        b.controlId = pendingMidiLearnControl_;
        b.messageType = msg.isCC() ? "cc" : "note";
        b.number = static_cast<int>(msg.data1);
        b.channel = static_cast<int>(msg.channel());

        bool replaced = false;
        for (auto& existing : xCfg_.midiLearnBindings) {
            if (existing.controlId == b.controlId) {
                existing = b;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            xCfg_.midiLearnBindings.push_back(b);
        }

        rebuildMidiBindingCache();
        core::Logger::info("MIDI Learn: mapeado '" + b.controlId + "' -> " + bindingToLabel(b));
        pendingMidiLearnControl_.clear();
        return;
    }

    // Runtime mapping dispatch.
    for (const auto& [controlId, binding] : midiBindingByControl_) {
        if (!messageMatchesBinding(msg, binding)) continue;
        applyMidiControl(controlId, msg);
        return;
    }

    // Fallback legacy: any NoteOn triggers current active roll.
    if (msg.isNoteOn()) {
        const float vol = std::max(0.05f, static_cast<float>(msg.data2) / 127.0f);
        onTrigger(vol);
    }
}

void App::onTrigger(float volume) {
    if (!scheduler_) return;

    // If no roll button is active, do nothing.
    if (activeRollButton_ < 0 || activeRollButton_ > 4) {
        return;
    }

    selectRoll(activeRollButton_, volume);
}

void App::selectRoll(int rollButtonIndex, float volume) {
    if (!scheduler_) return;

    // Deselect behavior: clicking active button again turns roll off.
    if (rollButtonIndex < 0) {
        scheduler_->stopRoll(0);
        activeRollButton_ = -1;
        xCfg_.globalQuantization = -1;
        return;
    }

    if (rollButtonIndex > 4) return;

    const auto snap = linkManager_->snapshot();
    const auto div = toQuantDivisionFromRulerIndex(rollButtonIndex);

    scheduler_->startRoll(0, snap.beat, div, std::max(0.05f, volume));

    activeRollButton_ = rollButtonIndex;
    xCfg_.globalQuantization = rollButtonIndex;
}

void App::runHeadless() {
    core::Logger::info("Modo headless (sem GUI)");
    const auto startedAt  = std::chrono::steady_clock::now();
    const auto finishesAt = startedAt + cfg_.runFor;

    while (std::chrono::steady_clock::now() < finishesAt) {
        const auto snap = linkManager_->snapshot();
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2)
            << "mode=" << linkManager_->modeName()
            << " connected=" << (snap.connected ? "yes" : "no")
            << " peers=" << snap.peerCount
            << " bpm=" << snap.tempoBpm
            << " beat=" << snap.beat;
        core::Logger::info(oss.str());
        std::this_thread::sleep_for(cfg_.printEvery);
    }
    core::Logger::info("Headless finalizado.");
}

void App::runGui() {
    activeRollButton_ = (xCfg_.globalQuantization >= 0 && xCfg_.globalQuantization <= 4)
        ? xCfg_.globalQuantization
        : -1;

    xpad::gui::GuiHandlers handlers;
    handlers.onTrigger = [this]() {
        onTrigger(1.0f);
    };
    handlers.onMasterVolumeChange = [this](float v) {
        xCfg_.masterVolume = v;
        if (scheduler_) scheduler_->setMasterVolume(v);
    };
    handlers.onPitchChange = [this](float semitones) {
        xCfg_.pitchSemitones = semitones;
        if (scheduler_) scheduler_->setPitchSemitones(semitones);
    };
    handlers.onFilterChange = [this](float amount) {
        xCfg_.filterAmount = amount;
        if (scheduler_) scheduler_->setFilterAmount(amount);
    };
    handlers.onSampleSelectionChange = [this](int index) {
        if (index < 0 || index >= static_cast<int>(availableSamplePaths_.size())) return;
        selectedSampleIndex_ = index;
        loadSelectedSampleIntoPad0();
    };
    handlers.onRulerChange = [this](int rulerIndex) {
        selectRoll(rulerIndex, 1.0f);
    };
    handlers.onMidiPortChange = [this](const std::string& portName) {
        applyMidiPortSelection(portName);
    };
    handlers.onAudioDeviceChange = [this](const std::string& deviceName) {
        applyAudioDeviceSelection(deviceName);
    };
    handlers.onSetMidiLearnMode = [this](bool enabled) {
        setMidiLearnMode(enabled);
    };
    handlers.onBeginMidiLearn = [this](const std::string& control) {
        beginMidiLearn(control);
    };
    handlers.onGetMidiStatus = [this]() -> std::pair<std::string, std::string> {
        return {midiConnectedPortLabel_, midiLastMessageLabel_};
    };
    handlers.onSaveConfig = [this]() {
        xCfg_.save(xpad::config::XPadConfig::defaultPath());
    };

    window_ = std::make_unique<xpad::gui::MainWindow>(std::move(handlers));
    if (!window_->init(900, 420, "X-1000 | @manoelpiovesan")) {
        core::Logger::error("GUI: falha na inicializacao, rodando headless");
        runHeadless();
        return;
    }

    const auto midiPorts = midiManager_->listPorts();
    const auto audioDevices = xpad::audio::AudioEngine::listDevices();
    window_->run(*linkManager_,
                 *scheduler_,
                 xCfg_,
                 midiPorts,
                 audioDevices,
                 availableSampleNames_,
                 selectedSampleIndex_,
                 activeRollButton_,
                 midiLearnMode_,
                 pendingMidiLearnControl_,
                 midiBindingLabelByControl_);
}

} // namespace xpad::app


