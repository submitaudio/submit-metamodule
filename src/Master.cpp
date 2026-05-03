#include "plugin.hpp"
#include <cmath>

struct Master : Module {
    enum ParamIds {
        OUTPUT_PARAM,
        TRANSIENT_PARAM,
        GLUE_PARAM,
        WIDTH_PARAM,
        LIMIT_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        AUDIO_IN_L_INPUT,
        AUDIO_IN_R_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        AUDIO_OUT_L_OUTPUT,
        AUDIO_OUT_R_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        CLIP_LIGHT,
        NUM_LIGHTS
    };

    bool clipping = false;
    int clipTimer = 0;

    double rms = 0.0;
    double envGain = 1.0;
    double envFast = 0.0, envSlow = 0.0;

    float sampleRate = 44100.f;

    Master() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(OUTPUT_PARAM,    0.f, 1.5f, 1.f,  "Output",    "x", 0, 1);
        configParam(TRANSIENT_PARAM,-1.f, 1.f,  0.f,  "Transient", "%", 0, 100);
        configParam(GLUE_PARAM,     0.f,  1.f,  0.f,  "Glue",      "%", 0, 100);
        configParam(WIDTH_PARAM,    0.f,  2.f,  1.f,  "Width",     "x", 0, 1);
        configParam(LIMIT_PARAM,    0.f,  1.f,  0.f,  "Limit",     "%", 0, 100);
        configInput(AUDIO_IN_L_INPUT,  "Audio In L");
        configInput(AUDIO_IN_R_INPUT,  "Audio In R");
        configOutput(AUDIO_OUT_L_OUTPUT, "Audio Out L");
        configOutput(AUDIO_OUT_R_OUTPUT, "Audio Out R");
    }

    void onSampleRateChange() override {
        sampleRate = APP->engine->getSampleRate();
    }

    void process(const ProcessArgs& args) override {
        double inL = inputs[AUDIO_IN_L_INPUT].getVoltage();
        double inR = inputs[AUDIO_IN_R_INPUT].isConnected() ?
                     inputs[AUDIO_IN_R_INPUT].getVoltage() : inL;

        float glueAmt    = params[GLUE_PARAM].getValue();
        float transAmt   = params[TRANSIENT_PARAM].getValue();
        float widthAmt   = params[WIDTH_PARAM].getValue();
        float limitAmt   = params[LIMIT_PARAM].getValue();
        float outputGain = params[OUTPUT_PARAM].getValue();

        // 1. WARMTE
        double warmDrive = 0.15;
        double warmThresh = 0.01;
        if (fabs(inL) > warmThresh)
            inL = inL + warmDrive * inL * inL * (inL > 0 ? 1.0 : -1.0) * 0.1;
        if (fabs(inR) > warmThresh)
            inR = inR + warmDrive * inR * inR * (inR > 0 ? 1.0 : -1.0) * 0.1;

        // 2. TRANSIENT SHAPER
        if (fabs(transAmt) > 0.01f) {
            double inAbs = (fabs(inL) + fabs(inR)) * 0.5;
            double fast = exp(-1.0 / (0.001 * sampleRate));
            double slow = exp(-1.0 / (0.010 * sampleRate));
            envFast = envFast * fast + inAbs * (1.0 - fast);
            envSlow = envSlow * slow + inAbs * (1.0 - slow);
            double transient = envFast - envSlow;
            double gain = 1.0 + transAmt * transient * 2.0;
            if (gain < 0.5) gain = 0.5;
            if (gain > 2.0) gain = 2.0;
            static double smoothGain = 1.0;
            smoothGain = smoothGain * 0.99 + gain * 0.01;
            inL *= smoothGain;
            inR *= smoothGain;
        }

        // 3. GLUE COMPRESSOR
        if (glueAmt > 0.01f) {
            double threshold = 1.0 - glueAmt * 0.6;
            double ratio = 1.0 + glueAmt * 6.0;
            double attack  = exp(-1.0 / (0.010 * sampleRate));
            double release = exp(-1.0 / (0.200 * sampleRate));

            double inAbs = (fabs(inL) + fabs(inR)) * 0.5;
            rms = rms * 0.999 + inAbs * inAbs * 0.001;
            double rmsVal = sqrt(rms);

            double targetGain = 1.0;
            if (rmsVal > threshold) {
                targetGain = threshold + (rmsVal - threshold) / ratio;
                targetGain /= rmsVal;
            }
            double env = targetGain < envGain ? attack : release;
            envGain = envGain * env + targetGain * (1.0 - env);
            inL *= envGain;
            inR *= envGain;
        }

        // 4. WIDTH
        if (fabs(widthAmt - 1.0f) > 0.01f) {
            double mid  = (inL + inR) * 0.5;
            double side = (inL - inR) * 0.5;
            side *= widthAmt;
            inL = mid + side;
            inR = mid - side;
        }

        // 5. LIMITER
        if (limitAmt > 0.01f) {
            double ceiling = 5.0 * (1.0 - limitAmt * 0.25);
            double knee = 1.5;
            static double limGain = 1.0;
            double peakL = fabs(inL), peakR = fabs(inR);
            double peak = peakL > peakR ? peakL : peakR;
            double targetLimGain = peak > ceiling ? ceiling / peak : 1.0;
            double limAttack  = exp(-1.0 / (0.0001 * sampleRate));
            double limRelease = exp(-1.0 / (0.100 * sampleRate));
            double limEnv = targetLimGain < limGain ? limAttack : limRelease;
            limGain = limGain * limEnv + targetLimGain * (1.0 - limEnv);
            inL *= limGain;
            inR *= limGain;
        }

        // 6. OUTPUT GAIN
        inL *= outputGain;
        inR *= outputGain;

        // Clip detectie
        if (fabs(inL) > 5.f || fabs(inR) > 5.f) {
            clipping = true;
            clipTimer = (int)sampleRate / 4;
        }
        if (clipTimer > 0) clipTimer--;
        else clipping = false;
        lights[CLIP_LIGHT].setBrightness(clipping ? 1.f : 0.f);

        outputs[AUDIO_OUT_L_OUTPUT].setVoltage((float)inL);
        outputs[AUDIO_OUT_R_OUTPUT].setVoltage((float)inR);
    }
};

struct MasterWidget : ModuleWidget {
    MasterWidget(Master* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Master.png")));

        addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(Vec(22.79f, 34.75f)), module, Master::OUTPUT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(13.76f, 58.65f)), module, Master::TRANSIENT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(31.22f, 58.65f)), module, Master::GLUE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(13.76f, 77.74f)), module, Master::WIDTH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(31.25f, 77.74f)), module, Master::LIMIT_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.75f, 102.69f)), module, Master::AUDIO_IN_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.75f, 116.21f)), module, Master::AUDIO_IN_R_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(34.22f, 102.69f)), module, Master::AUDIO_OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(34.22f, 116.21f)), module, Master::AUDIO_OUT_R_OUTPUT));

        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(15.05f, 21.22f)), module, Master::CLIP_LIGHT));
    }
};

Model* modelMaster = createModel<Master, MasterWidget>("Master");
