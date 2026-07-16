#include "config/Config.hpp"
#include "core/Logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace xpad::config {

using json = nlohmann::json;

bool XPadConfig::save(const std::string& filePath) const {
    try {
        json j;
        j["audio"]["device"]        = audioDevice;
        j["audio"]["sampleRate"]    = sampleRate;
        j["audio"]["bufferFrames"]  = bufferSizeFrames;

        j["midi"]["port"]   = midiPortName;
        auto& mappings = j["midi"]["mappings"] = json::array();
        for (const auto& m : padMappings) {
            mappings.push_back({
                {"pad",    m.padIndex},
                {"note",   m.noteNumber},
                {"volume", m.volume},
                {"quant",  m.quantizationDiv},
            });
        }

        j["bank"]["active"]    = activeBankName;
        j["bank"]["directory"] = samplesDirectory;

        j["link"]["tempo"]   = initialTempoBpm;
        j["link"]["quantum"] = quantum;

        j["ui"]["masterVolume"]      = masterVolume;
        j["ui"]["pitchSemitones"]    = pitchSemitones;
        j["ui"]["filterAmount"]      = filterAmount;
        j["ui"]["selectedSamplePath"] = selectedSamplePath;
        j["ui"]["globalQuantization"] = globalQuantization;

        j["padQuantization"] = padQuantization;

        std::ofstream out(filePath);
        out << j.dump(2);
        core::Logger::info("Config: saved to " + filePath);
        return true;
    } catch (const std::exception& e) {
        core::Logger::error(std::string("Config::save error: ") + e.what());
        return false;
    }
}

bool XPadConfig::load(const std::string& filePath) {
    if (!std::filesystem::exists(filePath)) {
        core::Logger::warn("Config: file not found: " + filePath);
        return false;
    }
    try {
        std::ifstream in(filePath);
        json j;
        in >> j;

        audioDevice       = j.value("/audio/device"_json_pointer, std::string{});
        sampleRate        = j.value("/audio/sampleRate"_json_pointer, 48000u);
        bufferSizeFrames  = j.value("/audio/bufferFrames"_json_pointer, 256u);

        midiPortName = j.value("/midi/port"_json_pointer, std::string{});
        padMappings.clear();
        if (j.contains("midi") && j["midi"].contains("mappings")) {
            for (const auto& m : j["midi"]["mappings"]) {
                padMappings.push_back({
                    m.value("pad", -1),
                    static_cast<std::uint8_t>(m.value("note", 0)),
                    m.value("volume", 1.0f),
                    m.value("quant", 2),
                });
            }
        }

        activeBankName    = j.value("/bank/active"_json_pointer, std::string{"Default"});
        samplesDirectory  = j.value("/bank/directory"_json_pointer, std::string{"samples"});

        initialTempoBpm = j.value("/link/tempo"_json_pointer, 126.0);
        quantum         = j.value("/link/quantum"_json_pointer, 4.0);

        masterVolume      = j.value("/ui/masterVolume"_json_pointer, 1.0f);
        pitchSemitones    = j.value("/ui/pitchSemitones"_json_pointer, 0.0f);
        filterAmount      = j.value("/ui/filterAmount"_json_pointer, 0.0f);
        selectedSamplePath= j.value("/ui/selectedSamplePath"_json_pointer, std::string{});
        globalQuantization= j.value("/ui/globalQuantization"_json_pointer, 3);

        if (j.contains("padQuantization")) {
            auto& pq = j["padQuantization"];
            for (std::size_t i = 0; i < padQuantization.size() && i < pq.size(); ++i) {
                padQuantization[i] = pq[i].get<int>();
            }
        }

        core::Logger::info("Config: loaded from " + filePath);
        return true;
    } catch (const std::exception& e) {
        core::Logger::error(std::string("Config::load error: ") + e.what());
        return false;
    }
}

std::string XPadConfig::defaultPath() {
    return "xpad_config.json";
}

} // namespace xpad::config

