#include "plugin.hpp"
#include <algorithm>
#include <cmath>

template <const char* AssetPath>
struct MasterSubmitKnob : SvgKnob {
    MasterSubmitKnob() {
        minAngle = -0.83 * M_PI;
        maxAngle = 0.83 * M_PI;
        setSvg(Svg::load(asset::plugin(pluginInstance, AssetPath)));
        shadow->opacity = 0.f;
    }
};

static constexpr char MASTER_KNOB_MEDIUM_ASSET[] = "res/SubmitKnobMedium.png";
static constexpr char MASTER_KNOB_SMALL_ASSET[] = "res/SubmitKnobSmall.png";
using MasterSubmitKnobMedium = MasterSubmitKnob<MASTER_KNOB_MEDIUM_ASSET>;
using MasterSubmitKnobSmall = MasterSubmitKnob<MASTER_KNOB_SMALL_ASSET>;

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

    double outputSmooth = 1.0;
    double transientSmooth = 0.0;
    double glueSmooth = 0.0;
    double widthSmooth = 1.0;
    double limitSmooth = 0.0;

    double transientFast = 0.0;
    double transientSlow = 0.0;
    double transientGain = 1.0;

    double gluePower = 0.0;
    double glueGain = 1.0;

    double sideLowState1 = 0.0;
    double sideLowState2 = 0.0;

    double limiterGain = 1.0;

    double parameterCoeff = 0.0;
    double transientFastAttackCoeff = 0.0;
    double transientFastReleaseCoeff = 0.0;
    double transientSlowAttackCoeff = 0.0;
    double transientSlowReleaseCoeff = 0.0;
    double transientGainCoeff = 0.0;
    double glueAttackCoeff = 0.0;
    double glueReleaseCoeff = 0.0;
    double limiterReleaseCoeff = 0.0;
    double sideLowCoeff = 0.0;

    float sampleRate = 0.f;

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

    static double timeCoefficient(double seconds, double sr) {
        return std::exp(-1.0 / (std::max(seconds, 1.0e-6) * sr));
    }

    static double clampValue(double value, double minimum, double maximum) {
        return std::max(minimum, std::min(value, maximum));
    }

    static double sanitizeAudio(double sample) {
        if (!std::isfinite(sample))
            return 0.0;
        if (std::fabs(sample) < 1.0e-20)
            return 0.0;
        return clampValue(sample, -4.0, 4.0);
    }

    static double followEnvelope(double input, double& state,
                                 double attackCoeff, double releaseCoeff) {
        double coeff = input > state ? attackCoeff : releaseCoeff;
        state = input + coeff * (state - input);
        return state;
    }

    void updateSampleRate(float newSampleRate) {
        sampleRate = std::max(newSampleRate, 1000.f);
        const double sr = sampleRate;

        parameterCoeff = timeCoefficient(0.015, sr);
        transientFastAttackCoeff = timeCoefficient(0.0005, sr);
        transientFastReleaseCoeff = timeCoefficient(0.040, sr);
        transientSlowAttackCoeff = timeCoefficient(0.015, sr);
        transientSlowReleaseCoeff = timeCoefficient(0.120, sr);
        transientGainCoeff = timeCoefficient(0.002, sr);
        glueAttackCoeff = timeCoefficient(0.010, sr);
        glueReleaseCoeff = timeCoefficient(0.180, sr);
        limiterReleaseCoeff = timeCoefficient(0.120, sr);
        sideLowCoeff = std::exp(-2.0 * M_PI * 140.0 / sr);
    }

    void onSampleRateChange() override {
        updateSampleRate(APP->engine->getSampleRate());
    }

    void onReset() override {
        outputSmooth = 1.0;
        transientSmooth = 0.0;
        glueSmooth = 0.0;
        widthSmooth = 1.0;
        limitSmooth = 0.0;
        transientFast = 0.0;
        transientSlow = 0.0;
        transientGain = 1.0;
        gluePower = 0.0;
        glueGain = 1.0;
        sideLowState1 = 0.0;
        sideLowState2 = 0.0;
        limiterGain = 1.0;
        clipping = false;
        clipTimer = 0;
    }

    void process(const ProcessArgs& args) override {
        if (std::fabs(args.sampleRate - sampleRate) > 1.f)
            updateSampleRate(args.sampleRate);

        double inL = sanitizeAudio(inputs[AUDIO_IN_L_INPUT].getVoltage() * 0.2);
        double inR = inputs[AUDIO_IN_R_INPUT].isConnected()
            ? sanitizeAudio(inputs[AUDIO_IN_R_INPUT].getVoltage() * 0.2)
            : inL;

        auto smoothParameter = [&](double& state, double target) {
            state = target + parameterCoeff * (state - target);
        };
        smoothParameter(outputSmooth, params[OUTPUT_PARAM].getValue());
        smoothParameter(transientSmooth, params[TRANSIENT_PARAM].getValue());
        smoothParameter(glueSmooth, params[GLUE_PARAM].getValue());
        smoothParameter(widthSmooth, params[WIDTH_PARAM].getValue());
        smoothParameter(limitSmooth, params[LIMIT_PARAM].getValue());

        double transientDetector = std::max(std::fabs(inL), std::fabs(inR));
        followEnvelope(transientDetector, transientFast,
                       transientFastAttackCoeff, transientFastReleaseCoeff);
        followEnvelope(transientDetector, transientSlow,
                       transientSlowAttackCoeff, transientSlowReleaseCoeff);

        double transientDelta = (transientFast - transientSlow) / (transientSlow + 0.05);
        transientDelta = clampValue(transientDelta, -1.0, 1.0);
        double transientDb = clampValue(transientSmooth * transientDelta * 6.0, -6.0, 6.0);
        double transientTargetGain = std::exp(transientDb * (std::log(10.0) / 20.0));
        transientGain = transientTargetGain
            + transientGainCoeff * (transientGain - transientTargetGain);
        inL *= transientGain;
        inR *= transientGain;

        double power = 0.5 * (inL * inL + inR * inR);
        double gluePowerCoeff = power > gluePower ? glueAttackCoeff : glueReleaseCoeff;
        gluePower = power + gluePowerCoeff * (gluePower - power);
        double glueLevel = std::sqrt(std::max(gluePower, 0.0));

        double glueAmount = clampValue(glueSmooth, 0.0, 1.0);
        double thresholdDb = -2.0 - 10.0 * glueAmount;
        double ratio = 1.0 + 3.0 * glueAmount;
        constexpr double kneeDb = 3.0;
        double levelDb = 20.0 * std::log10(glueLevel + 1.0e-12);
        double overDb = levelDb - thresholdDb;
        double reductionDb = 0.0;
        double compressionSlope = 1.0 - 1.0 / ratio;
        if (overDb >= kneeDb * 0.5) {
            reductionDb = overDb * compressionSlope;
        } else if (overDb > -kneeDb * 0.5) {
            double kneePosition = overDb + kneeDb * 0.5;
            reductionDb = compressionSlope * kneePosition * kneePosition / (2.0 * kneeDb);
        }

        double glueTargetGain = std::exp(-reductionDb * (std::log(10.0) / 20.0));
        double glueCoeff = glueTargetGain < glueGain ? glueAttackCoeff : glueReleaseCoeff;
        glueGain = glueTargetGain + glueCoeff * (glueGain - glueTargetGain);
        double makeupGain = std::exp((1.5 * glueAmount) * (std::log(10.0) / 20.0));

        double dryGlueL = inL;
        double dryGlueR = inR;
        double wetGlueL = inL * glueGain * makeupGain;
        double wetGlueR = inR * glueGain * makeupGain;
        inL = dryGlueL + (wetGlueL - dryGlueL) * glueAmount;
        inR = dryGlueR + (wetGlueR - dryGlueR) * glueAmount;

        if (glueAmount > 1.0e-5) {
            double saturationDrive = 1.0 + 1.2 * glueAmount;
            double saturationNorm = std::tanh(saturationDrive);
            double warmMix = 0.12 * glueAmount;
            double saturatedL = std::tanh(inL * saturationDrive) / saturationNorm;
            double saturatedR = std::tanh(inR * saturationDrive) / saturationNorm;
            inL += (saturatedL - inL) * warmMix;
            inR += (saturatedR - inR) * warmMix;
        }

        double mid = (inL + inR) * 0.5;
        double side = (inL - inR) * 0.5;
        sideLowState1 = side + sideLowCoeff * (sideLowState1 - side);
        sideLowState2 = sideLowState1 + sideLowCoeff * (sideLowState2 - sideLowState1);
        double lowSide = sideLowState2;
        double highSide = side - lowSide;

        double widthAmount = clampValue(widthSmooth, 0.0, 2.0);
        double widenedSide;
        if (widthAmount <= 1.0) {
            widenedSide = side * widthAmount;
        } else {
            double lowWidth = 1.0 + (widthAmount - 1.0) * 0.15;
            widenedSide = lowSide * lowWidth + highSide * widthAmount;
        }
        inL = mid + widenedSide;
        inR = mid - widenedSide;

        inL *= outputSmooth;
        inR *= outputSmooth;

        double limitAmount = clampValue(limitSmooth, 0.0, 1.0);
        double limiterTargetGain = 1.0;
        if (limitAmount > 1.0e-5) {
            double ceiling = 1.20 - 0.45 * limitAmount;
            double knee = 0.05 + 0.10 * limitAmount;
            double kneeStart = ceiling - knee;
            double peak = std::max(std::fabs(inL), std::fabs(inR));
            if (peak > kneeStart) {
                double limitedPeak = kneeStart
                    + knee * (1.0 - std::exp(-(peak - kneeStart) / knee));
                limiterTargetGain = limitedPeak / std::max(peak, 1.0e-12);
            }
        }

        if (limiterTargetGain < limiterGain)
            limiterGain = limiterTargetGain;
        else
            limiterGain = limiterTargetGain
                + limiterReleaseCoeff * (limiterGain - limiterTargetGain);

        if (limitAmount > 1.0e-5) {
            inL *= limiterGain;
            inR *= limiterGain;
        }

        inL = sanitizeAudio(inL) * 5.0;
        inR = sanitizeAudio(inR) * 5.0;

        if (std::fabs(inL) > 5.f || std::fabs(inR) > 5.f) {
            clipping = true;
            clipTimer = (int)sampleRate / 4;
        }
        if (clipTimer > 0)
            clipTimer--;
        else
            clipping = false;
        lights[CLIP_LIGHT].setBrightness(clipping ? 1.f : 0.f);

        outputs[AUDIO_OUT_L_OUTPUT].setVoltage((float)inL);
        outputs[AUDIO_OUT_R_OUTPUT].setVoltage((float)inR);
    }
};

struct MasterWidget : ModuleWidget {
    MasterWidget(Master* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Master.png")));

        addParam(createParamCentered<MasterSubmitKnobMedium>(mm2px(Vec(22.79f, 34.75f)), module, Master::OUTPUT_PARAM));
        addParam(createParamCentered<MasterSubmitKnobSmall>(mm2px(Vec(13.76f, 58.65f)), module, Master::TRANSIENT_PARAM));
        addParam(createParamCentered<MasterSubmitKnobSmall>(mm2px(Vec(31.22f, 58.65f)), module, Master::GLUE_PARAM));
        addParam(createParamCentered<MasterSubmitKnobSmall>(mm2px(Vec(13.76f, 77.74f)), module, Master::WIDTH_PARAM));
        addParam(createParamCentered<MasterSubmitKnobSmall>(mm2px(Vec(31.25f, 77.74f)), module, Master::LIMIT_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.75f, 102.69f)), module, Master::AUDIO_IN_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.75f, 116.21f)), module, Master::AUDIO_IN_R_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(34.22f, 102.69f)), module, Master::AUDIO_OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(34.22f, 116.21f)), module, Master::AUDIO_OUT_R_OUTPUT));

        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(15.05f, 21.22f)), module, Master::CLIP_LIGHT));
    }
};

Model* modelMaster = createModel<Master, MasterWidget>("Master");
