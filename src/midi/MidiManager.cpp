#include "midi/MidiManager.hpp"
#include "core/Logger.hpp"
#include <RtMidi.h>
#include <algorithm>
#include <stdexcept>

namespace xpad::midi {

struct MidiManager::Impl {
    std::unique_ptr<RtMidiIn> midiIn;
    MidiCallback callback;
    std::string openPortName{};
    bool open{false};

    static void rtMidiCallback(double timestamp,
                                std::vector<unsigned char>* message,
                                void* userData) {
        auto* impl = static_cast<Impl*>(userData);
        if (!message || message->size() < 1 || !impl->callback) return;

        MidiMessage msg;
        msg.timestamp = timestamp;
        msg.status = (*message)[0];
        if (message->size() > 1) msg.data1 = (*message)[1];
        if (message->size() > 2) msg.data2 = (*message)[2];

        impl->callback(msg);
    }
};

MidiManager::MidiManager()
    : impl_(std::make_unique<Impl>()) {
    try {
        impl_->midiIn = std::make_unique<RtMidiIn>();
        impl_->midiIn->ignoreTypes(false, true, true); // listen to SysEx, ignore timing/active sensing
    } catch (const RtMidiError& e) {
        core::Logger::error(std::string("MidiManager: RtMidiIn init error: ") + e.getMessage());
    }
}

MidiManager::~MidiManager() {
    closePort();
}

std::vector<std::string> MidiManager::listPorts() const {
    std::vector<std::string> ports;
    if (!impl_->midiIn) return ports;
    const unsigned int count = impl_->midiIn->getPortCount();
    for (unsigned int i = 0; i < count; ++i) {
        ports.push_back(impl_->midiIn->getPortName(i));
    }
    return ports;
}

bool MidiManager::openPort(std::uint32_t portIndex) {
    if (!impl_->midiIn) return false;
    closePort();
    try {
        impl_->midiIn->openPort(portIndex);
        impl_->midiIn->setCallback(Impl::rtMidiCallback, impl_.get());
        impl_->openPortName = impl_->midiIn->getPortName(portIndex);
        impl_->open = true;
        core::Logger::info("MidiManager: opened port '" + impl_->openPortName + "'");
        return true;
    } catch (const RtMidiError& e) {
        core::Logger::error(std::string("MidiManager: openPort error: ") + e.getMessage());
        return false;
    }
}

bool MidiManager::openPortByName(const std::string& name) {
    if (!impl_->midiIn) return false;
    auto toLower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };

    const std::string wanted = toLower(name);
    const unsigned int count = impl_->midiIn->getPortCount();
    for (unsigned int i = 0; i < count; ++i) {
        const std::string portName = impl_->midiIn->getPortName(i);
        if (toLower(portName).find(wanted) != std::string::npos) {
            return openPort(i);
        }
    }
    core::Logger::warn("MidiManager: port '" + name + "' not found");
    return false;
}

void MidiManager::closePort() {
    if (!impl_->midiIn || !impl_->open) return;
    impl_->midiIn->closePort();
    impl_->open = false;
    core::Logger::info("MidiManager: closed port");
}

void MidiManager::setCallback(MidiCallback callback) {
    impl_->callback = std::move(callback);
}

bool MidiManager::isOpen() const noexcept {
    return impl_->open;
}

std::string MidiManager::openPortName() const noexcept {
    return impl_->openPortName;
}

std::vector<PadMapping> defaultLpd8Mapping() {
    return {
        {0, 40, 1.0f, 2}, // Pad 1 → note 40, 1/4
        {1, 41, 1.0f, 3}, // Pad 2 → note 41, 1/8
        {2, 42, 1.0f, 4}, // Pad 3 → note 42, 1/16
        {3, 43, 1.0f, 2}, // Pad 4 → note 43, 1/4
        {4, 44, 1.0f, 2}, // Pad 5 → note 44, 1/4
        {5, 45, 1.0f, 2}, // Pad 6 → note 45, 1/4
        {6, 46, 1.0f, 2}, // Pad 7 → note 46, 1/4
        {7, 47, 1.0f, 2}, // Pad 8 → note 47, 1/4
    };
}

} // namespace xpad::midi

