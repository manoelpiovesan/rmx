#pragma once
#include "audio/AudioEngine.hpp"
#include "audio/AudioScheduler.hpp"
#include "config/Config.hpp"
#include "gui/MainWindow.hpp"
#include "link/LinkManager.hpp"
#include "midi/MidiManager.hpp"
#include "samples/SampleBank.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xpad::app {

class App {
public:
    struct Config {
        double initialTempoBpm{126.0};
        double quantum{4.0};
        std::chrono::milliseconds printEvery{250};
        std::chrono::seconds runFor{5};
        bool simulateTempoChange{false};
        bool preferAbletonLink{true};
        bool headless{false};
        std::string configFile{};
    };

    App();
    explicit App(Config config);
    ~App();

    int run();

private:
    void initSubsystems();
    void shutdownSubsystems();
    void loadBank();
    void loadSelectedSampleIntoPad0();
    void runHeadless();
    void runGui();

    void onMidiMessage(const xpad::midi::MidiMessage& msg);
    void onTrigger(float volume);
    void selectRoll(int rollButtonIndex, float volume);
    void beginMidiLearn(const std::string& controlId);
    void setMidiLearnMode(bool enabled);
    void applyMidiPortSelection(const std::string& portName);
    void applyAudioDeviceSelection(const std::string& deviceName);
    void applyMidiControl(const std::string& controlId, const xpad::midi::MidiMessage& msg);
    void rebuildMidiBindingCache();

    Config cfg_;
    xpad::config::XPadConfig xCfg_;

    std::shared_ptr<xpad::samples::SampleBank>   bank_;
    std::shared_ptr<xpad::link::LinkManager>      linkManager_;
    std::shared_ptr<xpad::audio::AudioScheduler>  scheduler_;
    std::shared_ptr<xpad::audio::AudioEngine>     audioEngine_;
    std::shared_ptr<xpad::midi::MidiManager>      midiManager_;
    std::unique_ptr<xpad::gui::MainWindow>        window_;

    std::vector<std::string> availableSamplePaths_;
    std::vector<std::string> availableSampleNames_;
    int selectedSampleIndex_{0};
    int activeRollButton_{-1}; // 0..4 => 1/8,1/4,1/2,1/1,2/1
    bool midiLearnMode_{false};
    std::string pendingMidiLearnControl_{};
    std::unordered_map<std::string, xpad::config::MidiLearnBinding> midiBindingByControl_;
    std::unordered_map<std::string, std::string> midiBindingLabelByControl_;
    std::string midiConnectedPortLabel_{"(none)"};
    std::string midiLastMessageLabel_{"-"};

    bool tempoChanged_{false};
};

} // namespace xpad::app
