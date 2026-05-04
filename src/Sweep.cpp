// Copyright (c) 2025 Submit Audio (submitaudio.nl)
// Licensed under GPL v3 — see LICENSE file for details

#include "plugin.hpp"
#include <cmath>

struct SweepMuteButton : SvgSwitch {
    SweepMuteButton() {
        momentary = true;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/knob-reset-off.png")));
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/knob-reset-on.png")));
    }
};

struct BiquadFilter {
    float b0=1,b1=0,b2=0,a1=0,a2=0;
    float x1=0,x2=0,y1=0,y2=0;

    float process(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x;
        y2=y1; y1=y;
        return y;
    }

    void setLP(float freq, float q, float sr) {
        float w0 = 2.f * float(M_PI) * freq / sr;
        float alpha = std::sin(w0) / (2.f * q);
        float cosw0 = std::cos(w0);
        float a0 = 1.f + alpha;
        b0 = (1.f - cosw0) / 2.f / a0;
        b1 = (1.f - cosw0) / a0;
        b2 = (1.f - cosw0) / 2.f / a0;
        a1 = -2.f * cosw0 / a0;
        a2 = (1.f - alpha) / a0;
    }

    void setHP(float freq, float q, float sr) {
        float w0 = 2.f * float(M_PI) * freq / sr;
        float alpha = std::sin(w0) / (2.f * q);
        float cosw0 = std::cos(w0);
        float a0 = 1.f + alpha;
        b0 =  (1.f + cosw0) / 2.f / a0;
        b1 = -(1.f + cosw0) / a0;
        b2 =  (1.f + cosw0) / 2.f / a0;
        a1 = -2.f * cosw0 / a0;
        a2 = (1.f - alpha) / a0;
    }

    void reset() { x1=x2=y1=y2=0; }
};

struct SweepModule : Module {
    enum ParamId {
        SWEEP_KNOB_PARAM,
        RES_PARAM,
        RESET_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        SWEEP_CV_INPUT,
        RES_CV_INPUT,
        RESET_CV_INPUT,
        CHAIN_L_INPUT,
        CHAIN_R_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        CHAIN_L_OUTPUT,
        CHAIN_R_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        RESET_LIGHT,
        LIGHTS_LEN
    };

    BiquadFilter lpL, lpR, hpL, hpR;
    float smoothSweep = 0.5f;
    float smoothRes   = 0.f;

    SweepModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(SWEEP_KNOB_PARAM, 0.f, 1.f, 0.5f, "Sweep");
        configParam(RES_PARAM,        0.f, 1.f, 0.f,  "Resonance");
        configParam(RESET_PARAM,      0.f, 1.f, 0.f,  "Reset");
        configInput(SWEEP_CV_INPUT, "Sweep CV");
        configInput(RES_CV_INPUT,   "Resonance CV");
        configInput(RESET_CV_INPUT, "Reset CV");
        configInput(CHAIN_L_INPUT,  "Chain In L");
        configInput(CHAIN_R_INPUT,  "Chain In R");
        configOutput(CHAIN_L_OUTPUT, "Chain Out L");
        configOutput(CHAIN_R_OUTPUT, "Chain Out R");
    }

    void process(const ProcessArgs& args) override {
        bool resetActive = params[RESET_PARAM].getValue() > 0.5f;
        if (inputs[RESET_CV_INPUT].isConnected() && inputs[RESET_CV_INPUT].getVoltage() > 1.f)
            resetActive = true;

        if (resetActive) {
            float spd = 1.f / (0.20f * args.sampleRate);
            params[SWEEP_KNOB_PARAM].setValue(params[SWEEP_KNOB_PARAM].getValue() + spd * (0.5f - params[SWEEP_KNOB_PARAM].getValue()));
        }

        float sweepVal = clamp(params[SWEEP_KNOB_PARAM].getValue() + (inputs[SWEEP_CV_INPUT].isConnected() ? inputs[SWEEP_CV_INPUT].getVoltage() / 10.f : 0.f), 0.f, 1.f);
        float resVal   = clamp(params[RES_PARAM].getValue()        + (inputs[RES_CV_INPUT].isConnected()   ? inputs[RES_CV_INPUT].getVoltage()   / 10.f : 0.f), 0.f, 1.f);

        smoothSweep += 0.002f * (sweepVal - smoothSweep);
        smoothRes   += 0.002f * (resVal   - smoothRes);

        float inL = inputs[CHAIN_L_INPUT].getVoltage();
        float inR = inputs[CHAIN_R_INPUT].isConnected() ? inputs[CHAIN_R_INPUT].getVoltage() : inL;

        float deadzone = 0.01f;
        if (std::abs(smoothSweep - 0.5f) < deadzone) {
            outputs[CHAIN_L_OUTPUT].setVoltage(inL);
            outputs[CHAIN_R_OUTPUT].setVoltage(inR);
            lpL.reset(); lpR.reset();
            hpL.reset(); hpR.reset();
            lights[RESET_LIGHT].setBrightness(resetActive ? 1.f : 0.f);
            return;
        }

        float q = 0.707f + smoothRes * 4.f;

        if (smoothSweep < 0.5f) {
            float t = smoothSweep * 2.f;
            float freq = clamp(20.f * std::pow(1000.f, t), 20.f, 20000.f);
            lpL.setLP(freq, q, args.sampleRate);
            lpR.setLP(freq, q, args.sampleRate);
            outputs[CHAIN_L_OUTPUT].setVoltage(lpL.process(inL));
            outputs[CHAIN_R_OUTPUT].setVoltage(lpR.process(inR));
        } else {
            float t = (smoothSweep - 0.5f) * 2.f;
            float freq = clamp(20.f * std::pow(1000.f, t), 20.f, 20000.f);
            hpL.setHP(freq, q, args.sampleRate);
            hpR.setHP(freq, q, args.sampleRate);
            outputs[CHAIN_L_OUTPUT].setVoltage(hpL.process(inL));
            outputs[CHAIN_R_OUTPUT].setVoltage(hpR.process(inR));
        }

        lights[RESET_LIGHT].setBrightness(resetActive ? 1.f : 0.f);
    }

    json_t* dataToJson() override { return json_object(); }
    void dataFromJson(json_t* rootJ) override { (void)rootJ; }
};

struct SweepWidget : ModuleWidget {
    SweepWidget(SweepModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Sweep.svg")));

        addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(26.83f, 35.00f)), module, SweepModule::SWEEP_KNOB_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(26.75f, 58.94f)), module, SweepModule::RES_PARAM));
        addParam(createParamCentered<SweepMuteButton>(mm2px(Vec(26.91f, 75.59f)), module, SweepModule::RESET_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.33f, 35.37f)), module, SweepModule::SWEEP_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.33f, 59.03f)), module, SweepModule::RES_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.33f, 75.30f)), module, SweepModule::RESET_CV_INPUT));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.33f, 102.78f)), module, SweepModule::CHAIN_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(8.33f, 116.30f)), module, SweepModule::CHAIN_R_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.63f, 102.78f)), module, SweepModule::CHAIN_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.63f, 116.30f)), module, SweepModule::CHAIN_R_OUTPUT));
    }
};

Model* modelSweep = createModel<SweepModule, SweepWidget>("Sweep");
