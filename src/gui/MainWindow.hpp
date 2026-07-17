#pragma once
#include "audio/AudioEngine.hpp"
#include "audio/AudioScheduler.hpp"
#include "config/Config.hpp"
#include "link/LinkManager.hpp"
#include "midi/MidiManager.hpp"
#include "samples/SampleBank.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace xpad::gui {

struct GuiHandlers {
    std::function<void()>                           onTrigger;
    std::function<void(float volume)>               onMasterVolumeChange;
    std::function<void(float semitones)>            onPitchChange;
    std::function<void(float amount)>               onFilterChange;
    std::function<void(int sampleIndex)>            onSampleSelectionChange;
    std::function<void(int rulerIndex)>             onRulerChange;
    std::function<void(const std::string& portName)> onMidiPortChange;
    std::function<void(const std::string& deviceName)> onAudioDeviceChange;
    std::function<void(bool enabled)>               onSetMidiLearnMode;
    std::function<void(const std::string& control)> onBeginMidiLearn;
    std::function<std::pair<std::string, std::string>()> onGetMidiStatus;
    std::function<void()>                           onSaveConfig;
};

class MainWindow {
public:
    explicit MainWindow(GuiHandlers handlers);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool init(int width, int height, const char* title);
    void shutdown();

    void run(
        const xpad::link::LinkManager& linkManager,
        const xpad::audio::AudioScheduler& scheduler,
        xpad::config::XPadConfig& cfg,
        const std::vector<std::string>& midiPorts,
        const std::vector<std::string>& audioDevices,
        const std::vector<std::string>& sampleNames,
        int& selectedSampleIndex,
        int& activeRollButton,
        bool& midiLearnMode,
        std::string& pendingMidiLearnControl,
        const std::unordered_map<std::string, std::string>& midiBindings
    );

    [[nodiscard]] bool isOpen() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    GuiHandlers handlers_;
};

} // namespace xpad::gui

