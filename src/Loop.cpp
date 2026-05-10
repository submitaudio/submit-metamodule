// Copyright (c) 2025 Submit Audio (submitaudio.nl)
// Licensed under GPL v3 — see LICENSE file for details

#include "plugin.hpp"
#include <cmath>
#include <string>
#include <algorithm>
#include "filesystem/async_filebrowser.hh"
#include "wav/dr_wav.h"

struct Loop : Module {
    enum ParamIds {
        BARS_PARAM, BARSHIFT_PARAM, BPM_PARAM, SPEED_PARAM,
        SYNC_PARAM, REVERSE_PARAM, CUE_PARAM, RESET_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        CLOCK_INPUT, TRIG_INPUT, SPEED_CV_INPUT, BARS_CV_INPUT,
        BARSHIFT_CV_INPUT, BPM_CV_INPUT, REVERSE_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds { MAIN_L_OUTPUT, MAIN_R_OUTPUT, CUE_OUTPUT, NUM_OUTPUTS };
    enum LightIds { SYNC_LIGHT, REVERSE_LIGHT, CUE_LIGHT, NUM_LIGHTS };

    std::vector<float> bufL;
    std::vector<float> bufR;
    bool fileLoaded = false;
    std::string filePath = "";
    std::string fileName = "";
    float fileBpm = 0.f;
    float detectedBars = 0.f;
    unsigned fileSampleRate = 44100;
    unsigned totalFrames = 0;
    double playbackPos = 0.0;
    double stepAmount = 1.0;

    float clockPrev = 0.f;
    double clockBpm = 120.0;
    int clockSampleCount = 0;
    bool clockReceived = false;

    bool isReverse = false;
    bool reversePending = false;

    float trigPrev = 0.f;
    float resetBtnPrev = 0.f;
    bool resetPending = false;
    bool activeCue = false;
    bool fading = false;
    bool fadingOut = false;
    float fadeGain = 1.f;
    float lastCue = 0.f;

    Loop() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(BARS_PARAM, 0.f, 24.f, 0.f, "Bars (0=auto)");
        getParamQuantity(BARS_PARAM)->snapEnabled = true;
        configParam(BARSHIFT_PARAM, 1.f, 8.f, 1.f, "Bar Shift");
        getParamQuantity(BARSHIFT_PARAM)->snapEnabled = true;
        configParam(BPM_PARAM, 0.f, 300.f, 0.f, "BPM (0=auto)");
        configParam(SPEED_PARAM, -1.f, 1.f, 0.f, "Speed");
        configParam(SYNC_PARAM, 0.f, 1.f, 1.f, "Sync");
        configParam(REVERSE_PARAM, 0.f, 1.f, 0.f, "Reverse");
        configParam(CUE_PARAM, 0.f, 1.f, 0.f, "Cue");
        configParam(RESET_PARAM, 0.f, 1.f, 0.f, "Reset");
        configInput(CLOCK_INPUT, "Clock");
        configInput(TRIG_INPUT, "Reset");
        configInput(SPEED_CV_INPUT, "Speed CV");
        configInput(BARS_CV_INPUT, "Bars CV");
        configInput(BARSHIFT_CV_INPUT, "Bar Shift CV");
        configInput(BPM_CV_INPUT, "BPM CV");
        configInput(REVERSE_CV_INPUT, "Reverse CV");
        configOutput(MAIN_L_OUTPUT, "Main L");
        configOutput(MAIN_R_OUTPUT, "Main R");
        configOutput(CUE_OUTPUT, "Cue");
    }

    void readLI(double pos, float &outL, float &outR) {
        unsigned int idx = (unsigned int)pos;
        if (idx + 1 >= totalFrames) {
            outL = (idx < totalFrames) ? bufL[idx] : 0.f;
            outR = (idx < totalFrames) ? bufR[idx] : 0.f;
            return;
        }
        float dist = (float)(pos - (double)idx);
        outL = bufL[idx] + (bufL[idx+1] - bufL[idx]) * dist;
        outR = bufR[idx] + (bufR[idx+1] - bufR[idx]) * dist;
    }

    void loadSample(const std::string& path) {
        drwav wav;
        if (!drwav_init_file(&wav, path.c_str(), nullptr)) return;

        unsigned int frames = (unsigned int)wav.totalPCMFrameCount;
        unsigned int channels = wav.channels;

        std::vector<float> interleaved(frames * channels);
        drwav_read_pcm_frames_f32(&wav, frames, interleaved.data());
        drwav_uninit(&wav);

        bufL.resize(frames);
        bufR.resize(frames);

        for (unsigned int i = 0; i < frames; i++) {
            bufL[i] = interleaved[i * channels] * 5.f;
            bufR[i] = (channels >= 2) ? interleaved[i * channels + 1] * 5.f : bufL[i];
        }

        filePath = path;
        fileLoaded = true;
        fileSampleRate = wav.sampleRate;
        totalFrames = frames;
        playbackPos = 0.0;
        isReverse = false;
        reversePending = false;
        resetPending = false;
        activeCue = false;
        fading = false;
        fadingOut = false;
        fadeGain = 1.f;
        lastCue = 0.f;

        size_t sep = path.find_last_of("/\\");
        fileName = (sep != std::string::npos) ? path.substr(sep + 1) : path;

        fileBpm = 0.f;
        std::string lower = fileName;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        size_t bpmPos = lower.find("bpm");
        if (bpmPos != std::string::npos && bpmPos > 0) {
            size_t start = bpmPos;
            while (start > 0 && std::isdigit((unsigned char)lower[start-1])) start--;
            std::string bpmStr = lower.substr(start, bpmPos - start);
            if (!bpmStr.empty()) fileBpm = (float)std::atof(bpmStr.c_str());
        }

        detectedBars = 0.f;
        if (fileBpm > 0.f && totalFrames > 0) {
            float durationSec = (float)totalFrames / (float)fileSampleRate;
            float beats = durationSec / (60.f / fileBpm);
            float bars = beats / 4.f;
            float rounded = std::round(bars);
            if (std::abs(bars - rounded) < 0.15f && rounded >= 1.f)
                detectedBars = rounded;
        }

        updateStepAmount(APP->engine->getSampleRate());
    }

    void updateStepAmount(float sampleRate) {
        stepAmount = (double)fileSampleRate / (double)sampleRate;
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        updateStepAmount(e.sampleRate);
    }

    void process(const ProcessArgs& args) override {
        if (!fileLoaded || totalFrames == 0) {
            outputs[MAIN_L_OUTPUT].setVoltage(0.f);
            outputs[MAIN_R_OUTPUT].setVoltage(0.f);
            outputs[CUE_OUTPUT].setVoltage(0.f);
            return;
        }

        float barsParam = params[BARS_PARAM].getValue();
        float bars = (barsParam > 0.f) ? barsParam : detectedBars;
        if (bars <= 0.f) bars = 4.f;
        double framesPerBar = (double)totalFrames / (double)bars;

        float barShift = params[BARSHIFT_PARAM].getValue();
        if (inputs[BARSHIFT_CV_INPUT].isConnected())
            barShift = clamp(barShift + inputs[BARSHIFT_CV_INPUT].getVoltage() * 0.8f, 1.f, 8.f);
        barShift = std::round(barShift);
        unsigned int startFrame = (unsigned int)(((double)barShift - 1.0) * framesPerBar);
        if (startFrame >= totalFrames) startFrame = 0;

        float resetBtn = params[RESET_PARAM].getValue();
        float trigIn = inputs[TRIG_INPUT].getVoltage();
        if ((resetBtnPrev < 0.5f && resetBtn >= 0.5f) || (trigPrev < 1.f && trigIn >= 1.f))
            resetPending = true;
        resetBtnPrev = resetBtn;
        trigPrev = trigIn;

        float reverseSwitch = params[REVERSE_PARAM].getValue();
        if (inputs[REVERSE_CV_INPUT].isConnected())
            reverseSwitch = (inputs[REVERSE_CV_INPUT].getVoltage() > 1.f) ? 1.f : 0.f;
        bool reverseWanted = reverseSwitch > 0.5f;
        if (reverseWanted != isReverse && !reversePending)
            reversePending = true;

        float currentCue = params[CUE_PARAM].getValue();
        if (currentCue != lastCue) {
            if (currentCue < 0.5f) { activeCue = false; fadeGain = 0.f; fading = true; }
            else { fadingOut = true; fading = false; }
            lastCue = currentCue;
        }

        if (fading) { fadeGain += 1.f / args.sampleRate; if (fadeGain >= 1.f) { fadeGain = 1.f; fading = false; } }
        if (fadingOut) { fadeGain -= 1.f / (0.1f * args.sampleRate); if (fadeGain <= 0.f) { fadeGain = 0.f; fadingOut = false; activeCue = true; } }

        float clockIn = inputs[CLOCK_INPUT].getVoltage();
        if (clockPrev < 1.f && clockIn >= 1.f) {
            if (clockReceived && clockSampleCount > 0) {
                double interval = (double)clockSampleCount / args.sampleRate;
                double measured = 60.0 / interval;
                if (measured > 20.0 && measured < 400.0)
                    clockBpm = clockBpm * 0.85 + measured * 0.15;
            }
            clockSampleCount = 0;
            clockReceived = true;
        }
        clockSampleCount++;
        clockPrev = clockIn;

        float bpmOverride = params[BPM_PARAM].getValue();
        if (inputs[BPM_CV_INPUT].isConnected())
            bpmOverride = clamp(bpmOverride + inputs[BPM_CV_INPUT].getVoltage() * 30.f, 0.f, 300.f);
        float sourceBpm = (bpmOverride > 0.f) ? bpmOverride : fileBpm;
        if (sourceBpm <= 0.f) sourceBpm = 120.f;

        bool sync = params[SYNC_PARAM].getValue() > 0.5f;
        double speedKnob = std::pow(2.0, (double)params[SPEED_PARAM].getValue());
        double speedCv = inputs[SPEED_CV_INPUT].isConnected() ? inputs[SPEED_CV_INPUT].getVoltage() / 5.0 : 0.0;
        double speedMult = speedKnob * std::pow(2.0, speedCv);
        speedMult = clamp((float)speedMult, 0.25f, 4.0f);

        double playSpeed;
        if (sync && clockReceived) {
            double effectiveBpm = clockBpm * speedMult;
            double loopDurationSec = (bars * 4.0 * 60.0) / effectiveBpm;
            double loopFrames = loopDurationSec * fileSampleRate;
            playSpeed = (double)totalFrames / loopFrames;
        } else {
            playSpeed = stepAmount * speedMult;
        }

        float outL = 0.f, outR = 0.f;
        readLI(playbackPos, outL, outR);

        // Huidige bar index
        double posInLoop = playbackPos - (double)startFrame;
        if (posInLoop < 0.0) posInLoop += (double)totalFrames;
        int currentBarIdx = (int)(posInLoop / framesPerBar);
        bool atBarBoundary = false;

        if (!isReverse) {
            playbackPos += playSpeed;
            if (playbackPos >= (double)totalFrames) {
                playbackPos = (double)startFrame + (playbackPos - (double)totalFrames);
                if (playbackPos >= (double)totalFrames) playbackPos = (double)startFrame;
                atBarBoundary = true;
            } else {
                double newPosInLoop = playbackPos - (double)startFrame;
                if (newPosInLoop < 0.0) newPosInLoop += (double)totalFrames;
                int newBarIdx = (int)(newPosInLoop / framesPerBar);
                if (newBarIdx != currentBarIdx) atBarBoundary = true;
            }
        } else {
            playbackPos -= playSpeed;
            if (playbackPos < (double)startFrame) {
                double overshoot = (double)startFrame - playbackPos;
                playbackPos = (double)totalFrames - overshoot;
                if (playbackPos >= (double)totalFrames) playbackPos = (double)totalFrames - 1.0;
                atBarBoundary = true;
            } else {
                double newPosInLoop = playbackPos - (double)startFrame;
                if (newPosInLoop < 0.0) newPosInLoop += (double)totalFrames;
                int newBarIdx = (int)(newPosInLoop / framesPerBar);
                if (newBarIdx != currentBarIdx) atBarBoundary = true;
            }
        }

        if (atBarBoundary) {
            if (resetPending) {
                playbackPos = (double)startFrame;
                resetPending = false;
            }
            if (reversePending) {
                isReverse = reverseWanted;
                reversePending = false;
            }
        }

        if (activeCue) {
            outputs[MAIN_L_OUTPUT].setVoltage(0.f);
            outputs[MAIN_R_OUTPUT].setVoltage(0.f);
            outputs[CUE_OUTPUT].setVoltage((outL + outR) * 0.5f * fadeGain);
        } else {
            outputs[MAIN_L_OUTPUT].setVoltage(outL * fadeGain);
            outputs[MAIN_R_OUTPUT].setVoltage(outR * fadeGain);
            outputs[CUE_OUTPUT].setVoltage(0.f);
        }

        lights[SYNC_LIGHT].setBrightness(sync ? 1.f : 0.f);
        lights[REVERSE_LIGHT].setBrightness(isReverse ? 1.f : 0.f);
        lights[CUE_LIGHT].setBrightness(activeCue ? 1.f : 0.f);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "filePath", json_string(filePath.c_str()));
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* fp = json_object_get(root, "filePath");
        if (fp) {
            std::string path = json_string_value(fp);
            if (!path.empty()) loadSample(path);
        }
    }
};

struct LoopWidget : ModuleWidget {
    LoopWidget(Loop* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Loop.png")));

        addParam(createParamCentered<rack::RoundSmallBlackKnob>(mm2px(Vec(19.002f, 60.229f)), module, Loop::BARS_PARAM));
        addParam(createParamCentered<rack::RoundSmallBlackKnob>(mm2px(Vec(39.291f, 60.229f)), module, Loop::BARSHIFT_PARAM));
        addParam(createParamCentered<rack::RoundSmallBlackKnob>(mm2px(Vec(19.002f, 83.833f)), module, Loop::BPM_PARAM));
        addParam(createParamCentered<rack::RoundSmallBlackKnob>(mm2px(Vec(39.376f, 83.918f)), module, Loop::SPEED_PARAM));

        addParam(createParamCentered<CKSS>(mm2px(Vec(55.657f, 63.360f)), module, Loop::SYNC_PARAM));
        addParam(createParamCentered<CKSS>(mm2px(Vec(55.657f, 86.832f)), module, Loop::REVERSE_PARAM));
        addParam(createParamCentered<CKSS>(mm2px(Vec(55.657f, 110.431f)), module, Loop::CUE_PARAM));

        addParam(createParamCentered<rack::RoundSmallBlackKnob>(mm2px(Vec(38.985f, 103.789f)), module, Loop::RESET_PARAM));

        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(51.795f, 57.076f)), module, Loop::SYNC_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(51.795f, 80.707f)), module, Loop::REVERSE_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(51.795f, 104.286f)), module, Loop::CUE_LIGHT));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(55.657f, 50.566f)), module, Loop::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(38.866f, 116.074f)), module, Loop::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.219f, 74.177f)), module, Loop::SPEED_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.885f, 50.566f)), module, Loop::BARS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.219f, 50.566f)), module, Loop::BARSHIFT_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.885f, 74.177f)), module, Loop::BPM_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(55.657f, 74.177f)), module, Loop::REVERSE_CV_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(9.885f, 116.074f)), module, Loop::MAIN_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.886f, 116.074f)), module, Loop::MAIN_R_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(55.657f, 97.877f)), module, Loop::CUE_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        Loop* module = dynamic_cast<Loop*>(this->module);
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Load WAV...", "", [=]() {
            async_dialog_filebrowser(false, ".wav,.WAV", nullptr, "Load WAV", [=](char* path) {
                if (path) {
                    module->loadSample(std::string(path));
                    free(path);
                }
            });
        }));
        if (module->fileLoaded) {
            menu->addChild(createMenuLabel(module->fileName));
            if (module->fileBpm > 0.f)
                menu->addChild(createMenuLabel("BPM: " + std::to_string((int)module->fileBpm)));
            if (module->detectedBars > 0.f)
                menu->addChild(createMenuLabel("Bars: " + std::to_string((int)module->detectedBars)));
        }
    }
};

Model* modelLoop = createModel<Loop, LoopWidget>("Loop");
