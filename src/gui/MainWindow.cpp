#include "gui/MainWindow.hpp"
#include "core/Logger.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace xpad::gui {

static const char* kRollLabels[] = {"1/8", "1/4", "1/2", "1/1", "2/1"};

struct MainWindow::Impl {
    GLFWwindow* window{nullptr};
    bool open{false};
};

MainWindow::MainWindow(GuiHandlers handlers)
    : impl_(std::make_unique<Impl>())
    , handlers_(std::move(handlers)) {
}

MainWindow::~MainWindow() {
    shutdown();
}

bool MainWindow::init(int width, int height, const char* title) {
    if (!glfwInit()) {
        core::Logger::error("MainWindow: glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    impl_->window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!impl_->window) {
        core::Logger::error("MainWindow: glfwCreateWindow failed");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(impl_->window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(impl_->window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    impl_->open = true;
    core::Logger::info("MainWindow: initialized");
    return true;
}

void MainWindow::shutdown() {
    if (!impl_->open) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (impl_->window) {
        glfwDestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    glfwTerminate();
    impl_->open = false;
}

bool MainWindow::isOpen() const noexcept {
    return impl_->open;
}

void MainWindow::run(const xpad::link::LinkManager& linkManager,
                     const xpad::audio::AudioScheduler& scheduler,
                     xpad::config::XPadConfig& cfg,
                     const std::vector<std::string>& midiPorts,
                     const std::vector<std::string>& audioDevices,
                     const std::vector<std::string>& sampleNames,
                     int& selectedSampleIndex,
                     int& activeRollButton,
                     bool& midiLearnMode,
                     std::string& pendingMidiLearnControl,
                     const std::unordered_map<std::string, std::string>& midiBindings) {
    (void)scheduler;
    (void)midiPorts;
    (void)audioDevices;
    auto bindingLabel = [&midiBindings](const std::string& id) -> std::string {
        auto it = midiBindings.find(id);
        return it == midiBindings.end() ? "-" : it->second;
    };

    while (!glfwWindowShouldClose(impl_->window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();

            auto makeComboItems = [](const std::string& defaultLabel,
                                     const std::vector<std::string>& values,
                                     std::vector<std::string>& storage,
                                     std::vector<const char*>& cstrStorage) {
                storage.clear();
                cstrStorage.clear();
                storage.push_back(defaultLabel);
                storage.insert(storage.end(), values.begin(), values.end());
                cstrStorage.reserve(storage.size());
                for (const auto& item : storage) cstrStorage.push_back(item.c_str());
            };

            auto indexFromValue = [](const std::vector<std::string>& values, const std::string& current) {
                if (current.empty()) return 0;
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (values[i] == current) return static_cast<int>(i + 1);
                }
                return 0;
            };
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const auto snapshot = linkManager.snapshot();

        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        int fbW{0}, fbH{0};
        glfwGetFramebufferSize(impl_->window, &fbW, &fbH);
        ImGui::SetNextWindowSize({static_cast<float>(fbW), static_cast<float>(fbH)}, ImGuiCond_Always);
        ImGui::Begin("X-1000 | @manoelpiovesan", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::Text("Link: %s | BPM: %.2f | Beat: %.2f",
                    snapshot.connected ? "Connected" : "Disconnected",
                    snapshot.tempoBpm,
                    snapshot.beat);

        if (handlers_.onGetMidiStatus) {
            const auto [portLabel, msgLabel] = handlers_.onGetMidiStatus();
            const bool midiConnected = !portLabel.empty() && portLabel != "(none)";
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  midiConnected ? ImVec4(0.12f, 0.85f, 0.35f, 1.0f)
                                                : ImVec4(0.95f, 0.45f, 0.30f, 1.0f));
            ImGui::Text("MIDI: %s", midiConnected ? portLabel.c_str() : "Nao conectado");
            ImGui::PopStyleColor();
            ImGui::TextDisabled("Last: %s", msgLabel.c_str());
        }

        std::vector<std::string> midiComboStorage;
        std::vector<const char*> midiComboItems;
        makeComboItems("Auto (first available)", midiPorts, midiComboStorage, midiComboItems);
        int midiSelected = indexFromValue(midiPorts, cfg.midiPortName);
        if (ImGui::Combo("MIDI Input", &midiSelected, midiComboItems.data(), static_cast<int>(midiComboItems.size()))) {
            const std::string selectedPort = (midiSelected <= 0 || midiSelected > static_cast<int>(midiPorts.size()))
                ? std::string{}
                : midiPorts[static_cast<std::size_t>(midiSelected - 1)];
            if (handlers_.onMidiPortChange) handlers_.onMidiPortChange(selectedPort);
        }

        std::vector<std::string> audioComboStorage;
        std::vector<const char*> audioComboItems;
        makeComboItems("Default output", audioDevices, audioComboStorage, audioComboItems);
        int audioSelected = indexFromValue(audioDevices, cfg.audioDevice);
        if (ImGui::Combo("Audio Output", &audioSelected, audioComboItems.data(), static_cast<int>(audioComboItems.size()))) {
            const std::string selectedDevice = (audioSelected <= 0 || audioSelected > static_cast<int>(audioDevices.size()))
                ? std::string{}
                : audioDevices[static_cast<std::size_t>(audioSelected - 1)];
            if (handlers_.onAudioDeviceChange) handlers_.onAudioDeviceChange(selectedDevice);
        }

        bool learn = midiLearnMode;
        if (ImGui::Checkbox("MIDI Learn Mode", &learn)) {
            midiLearnMode = learn;
            if (handlers_.onSetMidiLearnMode) handlers_.onSetMidiLearnMode(learn);
        }
        ImGui::SameLine();
        if (!pendingMidiLearnControl.empty()) {
            ImGui::Text("Aguardando MIDI para: %s", pendingMidiLearnControl.c_str());
        } else if (midiLearnMode) {
            ImGui::Text("Clique em MAP em um controle e mexa na controladora");
        }

        ImGui::Spacing();
        ImGui::PushItemWidth(480.0f);
        std::vector<const char*> sampleNamesC;
        sampleNamesC.reserve(sampleNames.size());
        for (const auto& s : sampleNames) sampleNamesC.push_back(s.c_str());

        if (!sampleNamesC.empty()) {
            int idx = std::clamp(selectedSampleIndex, 0, static_cast<int>(sampleNamesC.size() - 1));
            if (ImGui::Combo("##sample", &idx, sampleNamesC.data(), static_cast<int>(sampleNamesC.size()))) {
                selectedSampleIndex = idx;
                if (handlers_.onSampleSelectionChange) handlers_.onSampleSelectionChange(idx);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("MAP PREV") && midiLearnMode) {
                pendingMidiLearnControl = "sample_prev";
                if (handlers_.onBeginMidiLearn) handlers_.onBeginMidiLearn("sample_prev");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", bindingLabel("sample_prev").c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("MAP NEXT") && midiLearnMode) {
                pendingMidiLearnControl = "sample_next";
                if (handlers_.onBeginMidiLearn) handlers_.onBeginMidiLearn("sample_next");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", bindingLabel("sample_next").c_str());
        } else {
            ImGui::TextDisabled("Nenhum sample encontrado em /samples");
        }
        ImGui::PopItemWidth();

        const ImVec2 buttonSize{96.0f, 72.0f};
        for (int i = 0; i < 5; ++i) {
            if (i > 0) ImGui::SameLine(0.0f, 8.0f);

            const bool isActive = (activeRollButton == i);
            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.62f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.70f, 1.00f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.55f, 0.90f, 1.0f));
            }

            if (ImGui::Button(kRollLabels[i], buttonSize)) {
                if (midiLearnMode) {
                    pendingMidiLearnControl = "roll_" + std::to_string(i);
                    if (handlers_.onBeginMidiLearn) handlers_.onBeginMidiLearn(pendingMidiLearnControl);
                } else {
                    // Toggle: clicking the active button deselects it (stops roll)
                    if (isActive) {
                        activeRollButton = -1;
                        cfg.globalQuantization = -1;
                        if (handlers_.onRulerChange) handlers_.onRulerChange(-1);
                    } else {
                        activeRollButton = i;
                        cfg.globalQuantization = i;
                        if (handlers_.onRulerChange) handlers_.onRulerChange(i);
                    }
                }
            }

            if (isActive) {
                ImGui::PopStyleColor(3);
            }

            if (i == 4) {
                ImGui::NewLine();
            }
            ImGui::SameLine();
            const std::string rollId = "roll_" + std::to_string(i);
            ImGui::TextDisabled("%s", bindingLabel(rollId).c_str());
        }

        ImGui::Spacing();
        float mv = cfg.masterVolume;
        ImGui::PushItemWidth(240.0f);
        if (ImGui::SliderFloat("Master", &mv, 0.0f, 1.0f)) {
            cfg.masterVolume = mv;
            if (handlers_.onMasterVolumeChange) handlers_.onMasterVolumeChange(mv);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("MAP##master") && midiLearnMode) {
            pendingMidiLearnControl = "master";
            if (handlers_.onBeginMidiLearn) handlers_.onBeginMidiLearn("master");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", bindingLabel("master").c_str());
        ImGui::PopItemWidth();

        float pitch = cfg.pitchSemitones;
        ImGui::PushItemWidth(240.0f);
        if (ImGui::SliderFloat("Pitch", &pitch, -12.0f, 12.0f, "%.1f st")) {
            cfg.pitchSemitones = pitch;
            if (handlers_.onPitchChange) handlers_.onPitchChange(pitch);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("MAP##pitch") && midiLearnMode) {
            pendingMidiLearnControl = "pitch";
            if (handlers_.onBeginMidiLearn) handlers_.onBeginMidiLearn("pitch");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", bindingLabel("pitch").c_str());
        ImGui::PopItemWidth();

        float filter = cfg.filterAmount;
        ImGui::PushItemWidth(240.0f);
        if (ImGui::SliderFloat("Filter", &filter, -1.0f, 1.0f, "%.2f")) {
            cfg.filterAmount = filter;
            if (handlers_.onFilterChange) handlers_.onFilterChange(filter);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("MAP##filter") && midiLearnMode) {
            pendingMidiLearnControl = "filter";
            if (handlers_.onBeginMidiLearn) handlers_.onBeginMidiLearn("filter");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", bindingLabel("filter").c_str());
        ImGui::PopItemWidth();




        ImGui::End();

        ImGui::Render();
        glClearColor(0.12f, 0.12f, 0.13f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(impl_->window);
    }
}

} // namespace xpad::gui

