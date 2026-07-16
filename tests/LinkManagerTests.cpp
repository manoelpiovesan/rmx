#include "link/LinkManager.hpp"
#include "audio/AudioScheduler.hpp"
#include "samples/SampleBank.hpp"
#include "samples/Sample.hpp"
#include "config/Config.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstdlib>
#include <string>

namespace {

// ─── helpers ────────────────────────────────────────────────────────────────

// Get cross-platform temporary directory path
std::string getTempDir() {
#ifdef _WIN32
    const char* temp = std::getenv("TEMP");
    if (!temp) temp = std::getenv("TMP");
    if (!temp) temp = ".";
    return std::string(temp) + "\\";
#else
    return "/tmp/";
#endif
}

void assertNear(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message
                  << " | expected=" << expected
                  << " actual=" << actual
                  << " tolerance=" << tolerance << '\n';
        throw std::runtime_error(message);
    }
}

void assertTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        throw std::runtime_error(message);
    }
}

// Makes a minimal loaded sample with `frameCount` stereo float frames.
std::shared_ptr<xpad::samples::Sample> makeSample(
    xpad::samples::PadMode mode = xpad::samples::PadMode::OneShot,
    std::uint64_t frameCount = 4800)
{
    auto s = std::make_shared<xpad::samples::Sample>();
    s->meta.mode = mode;
    s->meta.referenceBpm = 0.0;
    s->meta.volume = 1.0;
    s->sampleRate = 48000;
    s->channels   = 2;
    s->frameCount = frameCount;
    s->data.assign(frameCount * 2, 0.5f);
    return s;
}

// ─── LinkManager tests ───────────────────────────────────────────────────────

void testSimulationBeatAdvances() {
    xpad::link::LinkManager manager({
        .initialTempoBpm = 120.0,
        .quantum = 4.0,
        .preferAbletonLink = false,
    });

    manager.start();
    const auto first = manager.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto second = manager.snapshot();

    assertNear(second.beat - first.beat, 0.5, 0.2,
               "Beat should advance ~0.5 beats in 250ms at 120 BPM");
    manager.stop();
}

void testTempoChangePreservesContinuity() {
    xpad::link::LinkManager manager({
        .initialTempoBpm = 120.0,
        .quantum = 4.0,
        .preferAbletonLink = false,
    });

    manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const double beatBefore = manager.snapshot().beat;

    manager.setTempoBpm(60.0);
    const double beatAfter = manager.snapshot().beat;
    assertNear(beatAfter, beatBefore, 0.05,
               "Tempo change should not create a beat discontinuity");

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assertNear(manager.snapshot().beat - beatAfter, 0.3, 0.15,
               "Beat progression should follow the new tempo");
    manager.stop();
}

void testBeatProjection() {
    xpad::link::LinkManager manager({
        .initialTempoBpm = 120.0,
        .quantum = 4.0,
        .preferAbletonLink = false,
    });

    manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto now = std::chrono::steady_clock::now();
    const double current = manager.snapshot().beat;
    const double projected = manager.beatAt(now + std::chrono::milliseconds(500));
    assertNear(projected - current, 1.0, 0.15,
               "Projected beat should be ~1 beat ahead after 500ms at 120 BPM");
    manager.stop();
}

void testModeIsSimulationWhenPreferred() {
    xpad::link::LinkManager manager({
        .initialTempoBpm = 126.0,
        .quantum = 4.0,
        .preferAbletonLink = false,
    });
    manager.start();
    assertTrue(manager.mode() == xpad::link::LinkMode::Simulation,
               "Manager should stay in simulation mode when requested");
    manager.stop();
}

// ─── Quantization tests ──────────────────────────────────────────────────────

void testQuantizeNextBeat() {
    using D = xpad::audio::QuantizeDivision;

    // 1/4 grid: beat 0.18 → next = 0.25
    assertNear(xpad::audio::quantizeNextBeat(0.18, D::Quarter), 0.25, 1e-9,
               "Quantize 1/4: 0.18 → 0.25");

    // 1/8 grid: beat 0.18 → next = 0.25
    assertNear(xpad::audio::quantizeNextBeat(0.18, D::Eighth), 0.25, 1e-9,
               "Quantize 1/8: 0.18 → 0.25");

    // 1/8 grid: beat 0.32 → next = 0.375
    assertNear(xpad::audio::quantizeNextBeat(0.32, D::Eighth), 0.375, 1e-9,
               "Quantize 1/8: 0.32 → 0.375");

    // 1/4: beat 1.0 exactly → next = 1.25
    assertNear(xpad::audio::quantizeNextBeat(1.0, D::Quarter), 1.25, 1e-9,
               "Quantize 1/4: 1.0 → 1.25");

    // 1/2: beat 2.7 → next = 3.0
    assertNear(xpad::audio::quantizeNextBeat(2.7, D::Half), 3.0, 1e-9,
               "Quantize 1/2: 2.7 → 3.0");

    // Example from INSTRUCTIONS: beat 542.32, 1/4 → 542.50
    assertNear(xpad::audio::quantizeNextBeat(542.32, D::Quarter), 542.50, 1e-9,
               "INSTRUCTIONS example: 542.32 → 542.50 at 1/4");

    // Example from INSTRUCTIONS: beat 420.18, 1/8 → 420.25
    assertNear(xpad::audio::quantizeNextBeat(420.18, D::Eighth), 420.25, 1e-9,
               "INSTRUCTIONS example: 420.18 → 420.25 at 1/8");
}

void testDivisionToBeats() {
    using D = xpad::audio::QuantizeDivision;
    // Each name is the fraction of ONE beat (e.g. 1/4 = 0.25 beats)
    assertNear(xpad::audio::divisionToBeats(D::Whole),        1.0,     1e-9, "1/1 = 1.0 beat");
    assertNear(xpad::audio::divisionToBeats(D::Half),         0.5,     1e-9, "1/2 = 0.5 beats");
    assertNear(xpad::audio::divisionToBeats(D::Quarter),      0.25,    1e-9, "1/4 = 0.25 beats");
    assertNear(xpad::audio::divisionToBeats(D::Eighth),       0.125,   1e-9, "1/8 = 0.125 beats");
    assertNear(xpad::audio::divisionToBeats(D::Sixteenth),    0.0625,  1e-9, "1/16 = 0.0625 beats");
    assertNear(xpad::audio::divisionToBeats(D::ThirtySecond), 0.03125, 1e-9, "1/32 = 0.03125 beats");
}

// ─── AudioScheduler tests ────────────────────────────────────────────────────

void testSchedulerFiresAtCorrectBeat() {
    auto bank = std::make_shared<xpad::samples::SampleBank>();
    bank->setPad(0, makeSample());

    xpad::audio::AudioScheduler scheduler(bank);

    // 1/4 grid from beat 0.0 → fires at beat 0.25
    scheduler.schedulePad(0, 0.0, xpad::audio::QuantizeDivision::Quarter, 1.0f);
    assertTrue(scheduler.isPadScheduled(0), "Pad 0 should be pending after schedulePad");

    std::vector<float> buf(512 * 2, 0.f);

    // Beat 0.1 → not yet fired (scheduled at 0.25)
    scheduler.processAudio(buf.data(), 512, 48000, 0.1, 120.0);
    assertTrue(!scheduler.isPadActive(0),
               "Pad 0 should not be active at beat 0.1 when scheduled for 0.25");

    // Beat 0.26 → must fire
    scheduler.processAudio(buf.data(), 512, 48000, 0.26, 120.0);
    assertTrue(scheduler.isPadActive(0),
               "Pad 0 should be active after beat 0.25");
    assertTrue(!scheduler.isPadScheduled(0),
               "Pad 0 should no longer be scheduled once fired");
}

void testSchedulerReleasesHoldPad() {
    auto bank = std::make_shared<xpad::samples::SampleBank>();
    bank->setPad(1, makeSample(xpad::samples::PadMode::Hold));

    xpad::audio::AudioScheduler scheduler(bank);

    // 1/4 from beat 0.0 → fires at beat 0.25
    scheduler.schedulePad(1, 0.0, xpad::audio::QuantizeDivision::Quarter, 1.0f);
    std::vector<float> buf(256 * 2, 0.f);
    scheduler.processAudio(buf.data(), 256, 48000, 0.3, 120.0); // fire
    assertTrue(scheduler.isPadActive(1), "Hold pad should be active after fire");

    scheduler.releasePad(1);
    assertTrue(!scheduler.isPadActive(1), "Hold pad should stop after release");
}

void testSchedulerOneShot() {
    auto bank = std::make_shared<xpad::samples::SampleBank>();
    // Very short sample: 100 frames → finishes quickly
    bank->setPad(2, makeSample(xpad::samples::PadMode::OneShot, 100));

    xpad::audio::AudioScheduler scheduler(bank);
    scheduler.schedulePad(2, 0.0, xpad::audio::QuantizeDivision::Quarter, 1.0f);

    std::vector<float> buf(512 * 2, 0.f);
    scheduler.processAudio(buf.data(), 512, 48000, 0.3, 120.0); // fire + run past end
    assertTrue(!scheduler.isPadActive(2),
               "One-shot pad should be inactive after its sample finishes");
}

// ─── Config tests ────────────────────────────────────────────────────────────

void testConfigRoundTrip() {
    const std::string path = getTempDir() + "xpad_test_config.json";

    xpad::config::XPadConfig original;
    original.sampleRate      = 44100;
    original.masterVolume    = 0.75f;
    original.initialTempoBpm = 138.0;
    original.midiPortName    = "TestDevice";
    original.padQuantization = {3,3,3,3,3,3,3,3};

    assertTrue(original.save(path), "Config should save without error");

    xpad::config::XPadConfig loaded;
    assertTrue(loaded.load(path), "Config should load without error");

    assertNear(loaded.sampleRate,      44100, 0, "sampleRate round-trips");
    assertNear(loaded.masterVolume,    0.75,  0.001, "masterVolume round-trips");
    assertNear(loaded.initialTempoBpm, 138.0, 0.001, "tempo round-trips");
    assertTrue(loaded.midiPortName == "TestDevice", "midiPortName round-trips");
    for (int i = 0; i < 8; ++i) {
        assertTrue(loaded.padQuantization[i] == 3, "padQuantization round-trips");
    }
}

} // namespace

int main() {
    testSimulationBeatAdvances();
    testTempoChangePreservesContinuity();
    testBeatProjection();
    testModeIsSimulationWhenPreferred();

    testQuantizeNextBeat();
    testDivisionToBeats();

    testSchedulerFiresAtCorrectBeat();
    testSchedulerReleasesHoldPad();
    testSchedulerOneShot();

    testConfigRoundTrip();

    std::cout << "All tests passed.\n";
    return 0;
}


