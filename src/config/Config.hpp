#pragma once
#include "audio/AudioScheduler.hpp"
#include "midi/MidiManager.hpp"
#include <array>
#include <string>
#include <vector>

namespace xpad::config {

struct XPadConfig {
    // Audio
    std::string audioDevice{};
    std::uint32_t sampleRate{48000};
    std::uint32_t bufferSizeFrames{256};

    // MIDI
    std::string midiPortName{};
    std::vector<xpad::midi::PadMapping> padMappings{};

    // Bank
    std::string activeBankName{"Default"};
    std::string samplesDirectory{"samples"};

    // Link
    double initialTempoBpm{126.0};
    double quantum{4.0};

    // UI
    float masterVolume{1.0f};
    float pitchSemitones{0.0f};
    float filterAmount{0.0f};

    // RMX-style global controls
    std::string selectedSamplePath{};
    int globalQuantization{3}; // 0=2/1, 1=1/1, 2=1/2, 3=1/4, 4=1/8

    // Legacy per-pad quantization (kept for backward compatibility)
    std::array<int, 8> padQuantization{2,2,2,2,2,2,2,2};

    bool save(const std::string& filePath) const;
    bool load(const std::string& filePath);

    [[nodiscard]] static std::string defaultPath();
};

} // namespace xpad::config

