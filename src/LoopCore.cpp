// Copyright (c) 2025 Submit Audio (submitaudio.nl)
// Native SmartCoreProcessor Loop voor MetaModule

#include "CoreModules/SmartCoreProcessor.hh"
#include "CoreModules/register_module.hh"
#include "CoreModules/elements/elements.hh"
#include "filesystem/async_filebrowser.hh"
#include "wav/wav_file_stream.hh"
#include "graphics/waveform_display.hh"
#include "threads/async_thread.hh"
#include <cmath>
#include <string>
#include <algorithm>
#include <array>
#include <atomic>

namespace MetaModule {
using enum Coords;

// Local element helpers
struct SubmitKnob : Knob {
    constexpr SubmitKnob() = default;
    constexpr SubmitKnob(BaseElement b, float def=0.5f, float mn=0, float mx=1)
        : Knob{{{b, "4ms/comp/knob9mm_x.png"}, def, mn, mx}} {}
};
struct SubmitJackIn : JackInput {
    constexpr SubmitJackIn() = default;
    constexpr SubmitJackIn(BaseElement b) : JackInput{{b, ""}} {}
};
struct SubmitJackOut : JackOutput {
    constexpr SubmitJackOut() = default;
    constexpr SubmitJackOut(BaseElement b) : JackOutput{{b, ""}} {}
};
struct SubmitSwitch : FlipSwitch {
    constexpr SubmitSwitch() = default;
    constexpr SubmitSwitch(BaseElement b, unsigned np=2, unsigned def=0) {
        this->x_mm = b.x_mm; this->y_mm = b.y_mm; this->coords = b.coords;
        this->short_name = b.short_name; this->image = "";
        this->num_pos = np; this->default_value = def;
        this->frames[0] = "4ms/comp/switch_down.png";
        this->frames[1] = "4ms/comp/switch_up.png";
    }
};

struct LoopCoreInfo : ModuleInfoBase {
    static constexpr std::string_view slug{"Loop"};
    static constexpr std::string_view png_filename{"Submit/Loop.png"};
    static constexpr std::string_view svgFilename{""};

    static constexpr std::array<Element, 23> Elements{{
        SubmitKnob{{19.002f, 60.229f, Center, "Bars",  ""}, 0.f,  0.f, 24.f},
        SubmitKnob{{39.291f, 60.229f, Center, "Shift", ""}, 0.5f, 1.f,  8.f},
        SubmitKnob{{19.002f, 83.833f, Center, "BPM",   ""}, 0.f,  0.f,300.f},
        SubmitKnob{{39.376f, 83.918f, Center, "Speed", ""}, 0.5f, 0.f,  1.f},

        SubmitSwitch{{55.657f,  63.360f, Center, "Sync",    ""}, 2, 1},
        SubmitSwitch{{55.657f,  86.832f, Center, "Reverse", ""}, 2, 0},
        SubmitSwitch{{55.657f, 110.431f, Center, "Cue",     ""}, 2, 0},

        MomentaryButton{{38.985f, 103.789f, Center, "Reset", ""}},

        SubmitJackIn{{55.657f,  50.566f, Center, "Clock",   ""}},
        SubmitJackIn{{38.866f, 116.074f, Center, "Trig",    ""}},
        SubmitJackIn{{30.219f,  74.177f, Center, "SpeedCV", ""}},
        SubmitJackIn{{ 9.885f,  50.566f, Center, "BarsCV",  ""}},
        SubmitJackIn{{30.219f,  50.566f, Center, "ShiftCV", ""}},
        SubmitJackIn{{ 9.885f,  74.177f, Center, "BpmCV",   ""}},
        SubmitJackIn{{55.657f,  74.177f, Center, "RevCV",   ""}},

        SubmitJackOut{{ 9.885f, 116.074f, Center, "OutL", ""}},
        SubmitJackOut{{22.886f, 116.074f, Center, "OutR", ""}},
        SubmitJackOut{{55.657f,  97.877f, Center, "Cue",  ""}},

        MonoLight{{51.795f,  57.076f, Center, "SyncLED", ""}},
        MonoLight{{51.795f,  80.707f, Center, "RevLED",  ""}},
        MonoLight{{51.795f, 104.286f, Center, "CueLED",  ""}},

        DynamicGraphicDisplay{{7.122f, 13.708f, TopLeft, "Wave", "", 50.644f, 27.184f}},

        AltParamAction{{{0.f, 0.f, Center, "Load WAV", ""}}, {".wav, .WAV"}},
    }};

    enum class Elem {
        BarsKnob, ShiftKnob, BpmKnob, SpeedKnob,
        SyncSwitch, ReverseSwitch, CueSwitch,
        ResetButton,
        ClockIn, TrigIn, SpeedCVIn, BarsCVIn, ShiftCVIn, BpmCVIn, RevCVIn,
        OutLOut, OutROut, CueOut,
        SyncLED, RevLED, CueLED,
        WaveDisplay,
        LoadWavAction,
    };
};

class LoopCore : public SmartCoreProcessor<LoopCoreInfo> {
    using enum LoopCoreInfo::Elem;

public:
    LoopCore() : fs_thread{this} {
        waveform.set_wave_color(0xFF, 0xFF, 0x00);
        waveform.set_bar_bg_color(0x45, 0x45, 0x21);
        waveform.set_bar_fg_color(0xFF, 0xFF, 0x00);
    }

    ~LoopCore() {
        fs_thread.stop();
        wavStream.unload();
    }

    void update() override {
        if (!fileLoaded || totalFrames == 0) {
            setOutput<OutLOut>(0.f);
            setOutput<OutROut>(0.f);
            setOutput<CueOut>(0.f);
            return;
        }

        // Buffer bijvullen check
        if (++bufCount >= 512) {
            bufCount = 0;
            needsBuffer = (wavStream.samples_available() < 2048);
        }

        // Reset
        bool resetBtn = getState<ResetButton>() == MomentaryButton::State_t::PRESSED;
        if (resetBtn) resetPending = true;

        float trigIn = getInput<TrigIn>().value_or(0.f);
        if (trigPrev < 1.f && trigIn >= 1.f) resetPending = true;
        trigPrev = trigIn;

        // CUE
        float curCue = getState<CueSwitch>();
        if (curCue != lastCue) {
            if (curCue < 0.5f) { activeCue = false; fadeGain = 0.f; fading = true; }
            else { fadingOut = true; fading = false; }
            lastCue = curCue;
        }
        if (fading)    { fadeGain += 1.f/sr; if (fadeGain>=1.f){fadeGain=1.f;fading=false;} }
        if (fadingOut) { fadeGain -= 1.f/(0.1f*sr); if (fadeGain<=0.f){fadeGain=0.f;fadingOut=false;activeCue=true;} }

        // Clock
        float clk = getInput<ClockIn>().value_or(0.f);
        if (clockPrev < 1.f && clk >= 1.f) {
            if (clockReceived && clockCount > 0) {
                double measured = 60.0 / ((double)clockCount / sr);
                if (measured > 20.0 && measured < 400.0)
                    clockBpm = clockBpm * 0.85 + measured * 0.15;
            }
            clockCount = 0; clockReceived = true;
            if (resetPending) {
                seekTarget = 0;
                needsSeek = true;
                wavStream.reset_playback_to_frame(0);
                currentFrame = 0; speedAccum = 0.0;
                resetPending = false;
                waveform.sync();
            }
        }
        clockCount++; clockPrev = clk;

        // BPM
        float bpmOvr = getState<BpmKnob>();
        if (isPatched<BpmCVIn>()) bpmOvr = std::max(0.f, std::min(300.f, bpmOvr + getInput<BpmCVIn>().value_or(0.f)*30.f));
        float srcBpm = (bpmOvr > 0.f) ? bpmOvr : fileBpm;
        if (srcBpm <= 0.f) srcBpm = 120.f;

        float bars = getState<BarsKnob>();
        if (bars <= 0.f) bars = detectedBars;
        if (bars <= 0.f) bars = 4.f;

        bool sync = getState<SyncSwitch>() > 0.5f;
        double spd = std::max(0.25, std::min(4.0,
            std::pow(2.0, (double)(getState<SpeedKnob>()*2.f-1.f)) *
            std::pow(2.0, isPatched<SpeedCVIn>() ? getInput<SpeedCVIn>().value_or(0.f)/5.0 : 0.0)));
        double srRatio = (double)fileSampleRate / sr;

        double playSpeed = sync && clockReceived
            ? ((double)totalFrames / ((bars*4.0*60.0/(clockBpm*spd))*fileSampleRate)) * srRatio
            : srRatio * spd;

        speedAccum += playSpeed;
        float oL = 0.f, oR = 0.f;

        while (speedAccum >= 1.0 && wavStream.samples_available() > 0) {
            oL = wavStream.pop_sample() * 5.f;
            oR = (wavStream.num_channels() >= 2 && wavStream.samples_available() > 0)
                ? wavStream.pop_sample() * 5.f : oL;
            currentFrame++; speedAccum -= 1.0;
            waveform.draw_sample((oL+oR)*0.1f);

            if (currentFrame >= totalFrames || wavStream.is_eof()) {
                float bs = std::round(std::max(1.f,std::min(8.f,
                    getState<ShiftKnob>() + (isPatched<ShiftCVIn>() ? getInput<ShiftCVIn>().value_or(0.f)*0.8f : 0.f))));
                seekTarget = (unsigned)((((double)bs-1.0)*totalFrames)/(double)bars);
                if (seekTarget >= totalFrames) seekTarget = 0;
                needsSeek = true;
                wavStream.reset_playback_to_frame(seekTarget);
                currentFrame = seekTarget; speedAccum = 0.0;
                waveform.sync();
            }
        }

        if (totalFrames > 0)
            waveform.set_cursor_position((float)currentFrame/(float)totalFrames);

        float mono = (oL+oR)*0.5f;
        if (activeCue) {
            setOutput<OutLOut>(0.f); setOutput<OutROut>(0.f);
            setOutput<CueOut>(mono*fadeGain);
        } else {
            setOutput<OutLOut>(oL*fadeGain); setOutput<OutROut>(oR*fadeGain);
            setOutput<CueOut>(0.f);
        }

        setLED<SyncLED>(sync ? 1.f : 0.f);
        setLED<RevLED>(0.f);
        setLED<CueLED>(activeCue ? 1.f : 0.f);
    }

    void set_param(int id, float val) override {
        SmartCoreProcessor::set_param(id, val);
        if (id == param_idx<LoadWavAction> && val == 1) {
            async_open_file("", ".wav, .WAV", "Load WAV:", [this](char* path) {
                if (path) { loadSample(std::string(path)); free(path); }
            });
            SmartCoreProcessor::set_param(id, 0);
        }
    }

    void set_samplerate(float s) override { sr = s; }

    void loadSample(const std::string& path) {
        wavStream.unload();
        if (!wavStream.load(path)) return;
        filePath = path;
        fileLoaded = true;
        currentFrame = 0; speedAccum = 0.0;
        resetPending = activeCue = fading = fadingOut = false;
        fadeGain = 1.f; lastCue = 0.f;
        fileSampleRate = wavStream.wav_sample_rate();
        totalFrames = wavStream.total_frames();

        size_t sep = path.find_last_of("/\\");
        std::string fn = (sep != std::string::npos) ? path.substr(sep+1) : path;
        fileBpm = 0.f;
        std::string lo = fn;
        std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
        size_t bp = lo.find("bpm");
        if (bp != std::string::npos && bp > 0) {
            size_t st = bp;
            while (st > 0 && std::isdigit((unsigned char)lo[st-1])) st--;
            std::string bs = lo.substr(st, bp-st);
            if (!bs.empty()) fileBpm = (float)std::atof(bs.c_str());
        }
        detectedBars = 0.f;
        if (fileBpm > 0.f && totalFrames > 0) {
            float brs = ((float)totalFrames/(float)fileSampleRate) / (60.f/fileBpm) / 4.f;
            float rnd = std::round(brs);
            if (std::abs(brs-rnd) < 0.15f && rnd >= 1.f) detectedBars = rnd;
        }
        seekTarget = 0; needsSeek = true; needsBuffer = true;
        waveform.sync();
    }

    // Async filesystem thread
    void async_filesystem() {
        if (needsSeek) {
            wavStream.seek_frame_in_file(seekTarget);
            needsSeek = false;
            needsBuffer = true;
        }
        if (needsBuffer) {
            wavStream.read_frames_from_file();
            needsBuffer = false;
        }
    }

    void show_graphic_display(int, std::span<uint32_t> buf, unsigned w, lv_obj_t *c) override {
        waveform.show_graphic_display(buf, w, c);
    }
    bool draw_graphic_display(int) override { return waveform.draw_graphic_display(); }
    void hide_graphic_display(int) override { waveform.hide_graphic_display(); }

    std::string save_state() override { return filePath; }
    void load_state(std::string_view s) override { if (!s.empty()) loadSample(std::string(s)); }

private:
    AsyncThread fs_thread;
    WavFileStream wavStream{8192};
    bool fileLoaded = false;
    std::string filePath;
    float fileBpm = 0.f;
    float detectedBars = 0.f;
    unsigned fileSampleRate = 44100;
    unsigned totalFrames = 0;
    unsigned currentFrame = 0;
    double speedAccum = 0.0;
    unsigned seekTarget = 0;
    std::atomic<bool> needsSeek{false};
    std::atomic<bool> needsBuffer{false};

    float sr = 48000.f;
    float clockPrev = 0.f;
    double clockBpm = 120.0;
    int clockCount = 0;
    bool clockReceived = false;
    float trigPrev = 0.f;
    bool resetPending = false;
    bool activeCue = false;
    bool fading = false;
    bool fadingOut = false;
    float fadeGain = 1.f;
    float lastCue = 0.f;
    int bufCount = 0;

    StreamingWaveformDisplay waveform{50.644f, 27.184f};

};

} // namespace MetaModule
