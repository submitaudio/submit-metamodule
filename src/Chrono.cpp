#include "plugin.hpp"
#include <cmath>

struct ChronoSurgeButton : SvgSwitch {
    ChronoSurgeButton() {
        momentary = true;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/ChronoSurge_0.png")));
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/ChronoSurge_1.png")));
    }
};

struct ChronoBreakButton : SvgSwitch {
    ChronoBreakButton() {
        momentary = true;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/ChronoBreak_0.png")));
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/ChronoBreak_1.png")));
    }
};

struct ChronoSubmitKnobMedium : SvgKnob {
    ChronoSubmitKnobMedium() {
        minAngle = -0.83 * M_PI;
        maxAngle = 0.83 * M_PI;
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/SubmitKnobMedium.png")));
        shadow->opacity = 0.f;
    }
};

struct ChronoSubmitKnobSmall : SvgKnob {
    ChronoSubmitKnobSmall() {
        minAngle = -0.83 * M_PI;
        maxAngle = 0.83 * M_PI;
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/SubmitKnobSmall.png")));
        shadow->opacity = 0.f;
    }
};
#include <cstring>

static const int MAX_BUFFER = 48000 * 4;
static constexpr float MAX_HEAD_MULTIPLIER = 1.5f;

static inline float wrapBufferPos(float pos) {
    pos = std::fmod(pos, (float)MAX_BUFFER);
    if (pos < 0.f)
        pos += (float)MAX_BUFFER;
    return pos;
}

static inline float finiteOr(float value, float fallback = 0.f) {
    return std::isfinite(value) ? value : fallback;
}

static inline float safeAudio(float value) {
    if (!std::isfinite(value))
        return 0.f;
    return clamp(value, -12.f, 12.f);
}

struct Chrono : Module {

    enum ParamId {
        TIME_PARAM,
        FEEDBACK_PARAM,
        MIX_PARAM,
        DRIVE_PARAM,
        TAPE_PARAM,
        HEADS_PARAM,
        DIVISION_PARAM,
        SPACING_PARAM,
        SPREAD_PARAM,
        SURGE_PARAM,
        BREAK_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        AUDIO_L_INPUT,
        AUDIO_R_INPUT,
        TIME_CV_INPUT,
        FEEDBACK_CV_INPUT,
        MIX_CV_INPUT,
        DRIVE_CV_INPUT,
        TAPE_CV_INPUT,
        HEADS_CV_INPUT,
        CLOCK_INPUT,
        SPACING_CV_INPUT,
        SPREAD_CV_INPUT,
        SURGE_CV_INPUT,
        BREAK_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        AUDIO_L_OUTPUT,
        AUDIO_R_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        SURGE_LIGHT,
        BREAK_LIGHT,
        LIGHTS_LEN
    };

    // ── Fast tanh approximatie (CPU optimalisatie) ──
    static inline float fast_tanh(float x) {
        float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    // ── Delay buffer ──────────────────────────
    float buffer[MAX_BUFFER] = {};
    int   writePos    = 0;
    float filterState = 0.f;
    float phase       = 0.f;

    // ── Clock sync ────────────────────────────
    bool  lastClockHigh        = false;
    int   clockSampleCount     = 0;
    int   clockInterval        = 0;
    float smoothedDelaySamples = 0.f;
    float targetDelaySamples   = 0.f;
    float smoothedSpacing      = 0.f;

    // ── Surge + Break smooth ──────────────────
    float fbSmooth    = 0.4f;
    float cachedWetGain = 0.f;
    float lastMix = -1.f;
    float driveSmooth = 0.2f;
    float toneSmooth  = 1.0f;

    // ── Freeze state ──────────────────────────
    float freezeGain    = 0.f;
    float freezeHpState = 0.f;

    // ── Brake state ───────────────────────────
    float brakeSpeed       = 1.f;
    float brakeSpeedSmooth = 1.f;

    // Momentary smooth bypass: 0 = Chrono mix, 1 = direct dry signal.
    float dryFade = 0.f;

    // New modules use Time as an extra clocked ratio. Legacy patches retain
    // the original clocked behavior when loaded.
    bool clockedTimeEnabled = true;

    // Drijvende lees posities per head
    float headReadPos[3] = {0.f, 0.f, 0.f};
    bool  headPosInit    = false;

    // ── Dry brake buffer ──────────────────────
    static const int DRY_BUFFER = 48000; // 1 sec buffer
    float dryBuffer[48000] = {};
    int   dryWritePos = 0;
    float dryReadPos  = 0.f;

    // ── Print-through state ───────────────────
    float printThroughState = 0.f;

    // ── Stereo spread state ───────────────────
    float spreadSmooth = 0.f;  // Smoothed print-through signal

    // ── Tape state ────────────────────────────
    float tapeFilterState = 0.f;
    float wowPhase        = 0.f;
    float flutterPhase    = 0.f;
    float wowRate         = 0.f;
    float flutterRate     = 0.f;
    float hissState       = 0.f;
    float dropoutGain     = 1.f;
    float dropoutTarget   = 1.f;
    int   dropoutTimer    = 0;
    uint32_t rngState     = 12345;

    // ── Divisies ──────────────────────────────
    static const int NUM_DIVISIONS = 5;
    const float divisions[NUM_DIVISIONS] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};

    // ── Head combinaties ──────────────────────
    // [positie][head] = volume factor
    // 3 heads: triplet(1/3), dotted(1/2), quarter(1/1)
    const float headMix[6][3] = {
        {0.5f, 1.0f, 1.0f},  // 0: SUB  — laag/hoog/hoog
        {1.0f, 1.0f, 0.5f},  // 1: DUB  — hoog/hoog/laag
        {0.5f, 0.5f, 1.0f},  // 2: QTR  — laag/laag/hoog
        {0.5f, 1.0f, 0.5f},  // 3: DOT  — laag/hoog/laag
        {1.0f, 0.5f, 0.5f},  // 4: TRP  — hoog/laag/laag
        {1.0f, 1.0f, 1.0f},  // 5: ALL  — gelijk/gelijk/gelijk
    };

    Chrono() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(TIME_PARAM,     0.f, 1.f, 0.3f, "Time");
        configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.5f, "Feedback");
        configParam(MIX_PARAM,      0.f, 1.f, 0.318f, "Mix");
        configParam(DRIVE_PARAM,    0.f, 1.f, 0.128f, "Drive");
        configParam(TAPE_PARAM,     0.f, 1.f, 0.324f, "Tape");
        configSwitch(HEADS_PARAM, 0.f, 5.f, 0.f, "Heads", {"SUB", "DUB", "QTR", "DOT", "TRP", "ALL"});
        paramQuantities[HEADS_PARAM]->snapEnabled = true;
        configParam(DIVISION_PARAM, 0.f, 4.f, 2.f,  "Division");
        paramQuantities[DIVISION_PARAM]->snapEnabled = true;
        configParam(SPACING_PARAM,  0.f, 4.f, 3.f,  "Offset");
        paramQuantities[SPACING_PARAM]->snapEnabled = true;
        configParam(SPREAD_PARAM,   0.f, 1.f, 1.f,  "Spread");
        configParam(SURGE_PARAM,    0.f, 1.f, 0.f,  "Surge");
        configParam(BREAK_PARAM,    0.f, 1.f, 0.f,  "Dry");

        configInput(AUDIO_L_INPUT,    "Audio L/Mono");
        configInput(AUDIO_R_INPUT,    "Audio R");
        configInput(TIME_CV_INPUT,    "Time CV");
        configInput(FEEDBACK_CV_INPUT,"Feedback CV");
        configInput(MIX_CV_INPUT,     "Mix CV");
        configInput(DRIVE_CV_INPUT,   "Drive CV");
        configInput(TAPE_CV_INPUT,    "Tape CV");
        configInput(HEADS_CV_INPUT,   "Heads CV");
        configInput(CLOCK_INPUT,      "Clock");
        configInput(SPACING_CV_INPUT, "Offset CV");
        configInput(SPREAD_CV_INPUT,  "Spread CV");
        configInput(SURGE_CV_INPUT,   "Surge Gate");
        configInput(BREAK_CV_INPUT,   "Dry Gate");

        configOutput(AUDIO_L_OUTPUT, "Audio L");
        configOutput(AUDIO_R_OUTPUT, "Audio R");

        std::memset(buffer, 0, sizeof(buffer));
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "clockedTimeVersion", json_integer(1));
        json_object_set_new(rootJ, "clockedTimeEnabled", json_boolean(clockedTimeEnabled));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        if (json_t* enabledJ = json_object_get(rootJ, "clockedTimeEnabled"))
            clockedTimeEnabled = json_boolean_value(enabledJ);
    }

    void fromJson(json_t* rootJ) override {
        json_t* dataJ = json_object_get(rootJ, "data");
        if (!dataJ || !json_object_get(dataJ, "clockedTimeVersion"))
            clockedTimeEnabled = false;
        Module::fromJson(rootJ);
    }

    void process(const ProcessArgs& args) override {

        // ── CV inputs koppelen aan parameters ─
        auto cvAdd = [&](int paramId, int inputId, float scale = 0.1f) -> float {
            float val = params[paramId].getValue();
            if (inputs[inputId].isConnected())
                val += inputs[inputId].getVoltage() * scale;
            if (!std::isfinite(val))
                val = 0.f;
            return clamp(val, 0.f, 1.f);
        };

        float timeKnob = cvAdd(TIME_PARAM,     TIME_CV_INPUT);
        float mix      = cvAdd(MIX_PARAM,      MIX_CV_INPUT);
        float drive    = cvAdd(DRIVE_PARAM,    DRIVE_CV_INPUT);
        float tone     = 1.0f;

        // ── FEEDBACK curve ────────────────────
        float fbRaw = clamp(params[FEEDBACK_PARAM].getValue()
            + (inputs[FEEDBACK_CV_INPUT].isConnected()
                ? inputs[FEEDBACK_CV_INPUT].getVoltage() * 0.1f : 0.f), 0.f, 1.f);
        // Subtle perceptual lift in the middle while preserving both endpoints.
        float feedbackCurve = 1.f + 0.12f * (4.f * fbRaw * (1.f - fbRaw));
        float feedback = clamp(fbRaw * feedbackCurve * 0.98f, 0.f, 0.999f);

        // ── SURGE — FREEZE ────────────────────
        bool surgePressed = params[SURGE_PARAM].getValue() > 0.5f
            || (inputs[SURGE_CV_INPUT].isConnected() && inputs[SURGE_CV_INPUT].getVoltage() > 1.f);

        float freezeTarget = surgePressed ? 1.f : 0.f;
        if (surgePressed) {
            float fadeInSpeed = feedback < 0.5f ? 0.001f + (feedback / 0.5f) * 0.004f : 0.005f;
            freezeGain += (freezeTarget - freezeGain) * fadeInSpeed;
        } else {
            // Exact linear release: a fully developed Surge reaches zero in 3 s.
            freezeGain = fmaxf(0.f, freezeGain - 1.f / (3.f * args.sampleRate));
        }

        // Let Surge feedback and drive follow the same three-second release.
        float surgeFeedback = fmaxf(feedback + 0.08f, 0.90f);
        float fbUsed = feedback + (surgeFeedback - feedback) * freezeGain;
        fbUsed = clamp(fbUsed, 0.f, 0.990f);
        float driveUsed = drive + 0.30f * freezeGain;

        fbSmooth    += (fbUsed    - fbSmooth)    * 0.008f;
        driveSmooth += (driveUsed - driveSmooth) * 0.008f;
        toneSmooth  += (tone      - toneSmooth)  * 0.008f;
        fbSmooth    = clamp(fbSmooth,    0.f, 0.999f);
        driveSmooth = clamp(driveSmooth, 0.f, 2.f);
        toneSmooth  = clamp(toneSmooth,  0.f, 1.f);

        // ── DRY — smooth momentary bypass ─────
        bool dryPressed = params[BREAK_PARAM].getValue() > 0.5f
            || (inputs[BREAK_CV_INPUT].isConnected() && inputs[BREAK_CV_INPUT].getVoltage() > 1.f);

        const float dryFadeInStep  = 1.f / args.sampleRate;
        const float dryFadeOutStep = 1.f / args.sampleRate;
        if (dryPressed)
            dryFade = fminf(1.f, dryFade + dryFadeInStep);
        else
            dryFade = fmaxf(0.f, dryFade - dryFadeOutStep);

        // ── LEDs ──────────────────────────────
        lights[SURGE_LIGHT].setBrightness(surgePressed ? 1.f : 0.f);
        lights[BREAK_LIGHT].setBrightness(dryPressed ? 1.f : 0.f);

        // ── DIVISION ──────────────────────────
        float divRaw = finiteOr(params[DIVISION_PARAM].getValue(), 2.f);
        int divIndex = clamp((int)roundf(divRaw), 0, NUM_DIVISIONS - 1);
        float ratio  = divisions[divIndex];

        // ── CLOCK / FREE MODE ─────────────────
        bool clockConnected = inputs[CLOCK_INPUT].isConnected();
        bool clockHigh = inputs[CLOCK_INPUT].getVoltage() > 1.0f;

        if (clockConnected) {
            if (clockHigh && !lastClockHigh) {
                if (clockSampleCount > 0) {
                    int minSamples = (int)(0.1f * args.sampleRate);
                    if (clockSampleCount >= minSamples && clockSampleCount <= MAX_BUFFER - 1) {
                        clockInterval = clockSampleCount;
                    }
                }
                clockSampleCount = 0;
            }
            lastClockHigh = clockHigh;
            clockSampleCount++;
            if (clockInterval > 0) {
                float clockedTimeRatio = 1.f;
                if (clockedTimeEnabled) {
                    static const float positions[9] = {
                        0.f, 0.075f, 0.15f, 0.225f, 0.30f,
                        0.475f, 0.65f, 0.825f, 1.f
                    };
                    static const float ratios[9] = {
                        0.25f, 1.f / 3.f, 0.5f, 2.f / 3.f, 1.f,
                        4.f / 3.f, 1.5f, 2.f, 4.f
                    };
                    int nearest = 0;
                    float nearestDistance = fabsf(timeKnob - positions[0]);
                    for (int i = 1; i < 9; ++i) {
                        float distance = fabsf(timeKnob - positions[i]);
                        if (distance < nearestDistance) {
                            nearest = i;
                            nearestDistance = distance;
                        }
                    }
                    clockedTimeRatio = ratios[nearest];
                }
                targetDelaySamples = (float)clockInterval * ratio * clockedTimeRatio;
            }
        } else {
            targetDelaySamples = timeKnob * 2.f * args.sampleRate;
        }

        const float maxSafeDelaySamples = ((float)MAX_BUFFER - 2.f) / MAX_HEAD_MULTIPLIER;
        targetDelaySamples = clamp(targetDelaySamples, 1.f, maxSafeDelaySamples);
        smoothedDelaySamples += (targetDelaySamples - smoothedDelaySamples) * 0.01f;
        // Bij brake: delay tijd wordt geleidelijk langer → pitch daalt
        float brakeMultiplier = 1.f + (1.f - brakeSpeedSmooth) * 4.f;
        float delaySamples = clamp(smoothedDelaySamples * brakeMultiplier, 1.f, maxSafeDelaySamples);

        // ── HEADS ─────────────────────────────
        float headsRaw = params[HEADS_PARAM].getValue()
            + (inputs[HEADS_CV_INPUT].isConnected()
                ? inputs[HEADS_CV_INPUT].getVoltage() * 0.5f : 0.f);
        headsRaw = finiteOr(headsRaw, 0.f);
        int headIndex = clamp((int)roundf(headsRaw), 0, 5);

        // Head tijden relatief aan delay tijd
        // Triplet = 2/3, Dotted = 3/2, Quarter = 1x
        float headTime[3];
        if (clockConnected && clockInterval > 0) {
            headTime[0] = delaySamples * (2.f / 3.f);  // Triplet
            headTime[1] = delaySamples * 1.0f;          // Beat
            headTime[2] = delaySamples * (3.f / 2.f);  // Dotted
        } else {
            headTime[0] = delaySamples * (2.f / 3.f);
            headTime[1] = delaySamples * 1.0f;
            headTime[2] = delaySamples * (3.f / 2.f);
        }

        // ── SPACING ───────────────────────────
        static const float spacingTable[5] = {0.0f, 0.15f, 0.35f, 0.6f, 0.9f};
        float spacingRaw = params[SPACING_PARAM].getValue()
            + (inputs[SPACING_CV_INPUT].isConnected()
                ? inputs[SPACING_CV_INPUT].getVoltage() * 0.4f : 0.f);
        spacingRaw = finiteOr(spacingRaw, 3.f);
        int spacingIndex    = clamp((int)roundf(spacingRaw), 0, 4);
        float spacingFactor = spacingTable[spacingIndex];
        float spacingTarget = delaySamples * spacingFactor;
        smoothedSpacing += (spacingTarget - smoothedSpacing) * 0.01f;

        // ── DELAY BUFFER LEZEN ────────────────


        float dryRaw = inputs[AUDIO_L_INPUT].getVoltage();
        float dryRawR = inputs[AUDIO_R_INPUT].isConnected()
            ? inputs[AUDIO_R_INPUT].getVoltage() : dryRaw;

        // Dry direct — perfecte L/R sync
        float dry = dryRaw;

        // ── TAPE STOP — drijvende lees posities ─
        // Init lees posities op eerste run
        if (!headPosInit) {
            for (int i = 0; i < 3; i++) {
                headReadPos[i] = wrapBufferPos((float)writePos - headTime[i]);
            }
            headPosInit = true;
        }

        auto readHead = [&](int idx, float offset) -> float {
            // Target: waar de head normaal zou staan
            offset = clamp(offset, 1.f, (float)MAX_BUFFER - 2.f);
            float target = wrapBufferPos((float)writePos - offset);

            // Schuif lees positie op met brakeSpeedSmooth
            // 1.0 = normaal, 0.0 = stilstand
            headReadPos[idx] += brakeSpeedSmooth;
            headReadPos[idx] = wrapBufferPos(headReadPos[idx]);

            // Bij normale speed: sync naar target als te ver af
            if (brakeSpeedSmooth > 0.95f) {
                float diff = target - headReadPos[idx];
                if (diff > (float)MAX_BUFFER * 0.5f)  diff -= (float)MAX_BUFFER;
                if (diff < -(float)MAX_BUFFER * 0.5f) diff += (float)MAX_BUFFER;
                if (fabsf(diff) > 100.f) {
                    headReadPos[idx] = target;
                }
            }

            // Lineaire interpolatie
            float fpos = headReadPos[idx];
            if (!std::isfinite(fpos))
                fpos = headReadPos[idx] = target;
            int   pos0 = ((int)fpos) % MAX_BUFFER;
            int   pos1 = (pos0 + 1) % MAX_BUFFER;
            float frac = fpos - floorf(fpos);
            return safeAudio(buffer[pos0] * (1.f - frac) + buffer[pos1] * frac);
        };

        float h0 = readHead(0, headTime[0]);
        float h1 = readHead(1, headTime[1]);
        float h2 = readHead(2, headTime[2]);

        // Mix heads volgens geselecteerde combinatie
        float w0 = headMix[headIndex][0];
        float w1 = headMix[headIndex][1];
        float w2 = headMix[headIndex][2];
        float wTotal = w0 + w1 + w2;
        float delayed = safeAudio((h0 * w0 + h1 * w1 + h2 * w2) / wTotal);

        // ── FEEDBACK PATH ─────────────────────
        const float alpha = 0.1f + toneSmooth * 0.5f;
        filterState += alpha * (delayed - filterState);
        float toned = filterState;

        float variation = 1.0f + 0.02f * sinf(phase);
        phase += 0.01f;

        float absToned = fabsf(toned);
        float dynamicDrive = 1.0f + driveSmooth * 2.5f + absToned * sqrtf(absToned);
        float driven = fast_tanh(toned * dynamicDrive * variation);

        driven *= 1.04f; // lichte gain compensatie

        // SURGE low-end control
        if (surgePressed) {
            static float surgeBassTight = 0.f;
            surgeBassTight += 0.08f * (driven - surgeBassTight);
            driven = driven - surgeBassTight * 0.35f;
        }

        float fb = driven * fbSmooth;
        fb *= 0.98f;

        // ── FREEZE buffer write ────────────────
        freezeHpState += 0.05f * (fb - freezeHpState);

        // Altijd dry + feedback — surge bevriest bovenop
        buffer[writePos] = safeAudio(dry + fb);
        writePos++;
        if (writePos >= MAX_BUFFER)
            writePos = 0;

        // ── TAPE PROCESSING ───────────────────
        float tape = clamp(params[TAPE_PARAM].getValue()
            + (inputs[TAPE_CV_INPUT].isConnected()
                ? inputs[TAPE_CV_INPUT].getVoltage() * 0.1f : 0.f), 0.f, 1.f);

        // Twee fasen
        float damageStage = clamp((tape - 0.5f) / 0.5f, 0.f, 1.f); // 50→100%: wobble+damage

        // RNG
        rngState = rngState * 1664525u + 1013904223u;
        float rnd = (float)(rngState >> 16) / 65535.f;
        rngState = rngState * 1664525u + 1013904223u;
        float rnd2 = (float)(rngState >> 16) / 65535.f;

        // ── LAAG 1: TAPE HISS (0→50%) ─────────
        // Hoog frequent — tape hiss karakter
        rngState = rngState * 1664525u + 1013904223u;
        float rawNoise = ((float)(rngState >> 16) / 65535.f) * 2.f - 1.f;
        // Hoogpass — alleen hoge frequenties
        hissState += 0.8f * (rawNoise - hissState);
        float hissHigh = rawNoise - hissState;
        // Hiss bouwt op tot 50% en blijft daarna gelijk

        // ── LAAG 2: WOBBLE (50→100%, clock sync) ─
        if (clockConnected && clockInterval > 0) {
            float clockHz = args.sampleRate / (float)clockInterval;
            wowRate     = clockHz * 0.12f + rnd * clockHz * 0.04f;
            flutterRate = clockHz * 0.7f  + rnd2 * clockHz * 0.2f;
        } else {
            wowRate     = 0.25f + rnd * 0.15f;
            flutterRate = 5.5f  + rnd2 * 2.5f;
        }
        wowPhase     += wowRate     / args.sampleRate;
        flutterPhase += flutterRate / args.sampleRate;
        if (wowPhase     > 1.f) wowPhase     -= 1.f;
        if (flutterPhase > 1.f) flutterPhase -= 1.f;

        // Wobble alleen boven 50%, niet tijdens freeze
        float freezeMute   = 1.f - freezeGain;
        float wowDepth     = damageStage * 0.030f * freezeMute;
        float flutterDepth = damageStage * 0.012f * freezeMute;
        // Snelle sin approximatie voor wow/flutter (parabool)
        auto fast_sin = [](float x) -> float {
            x = x - floorf(x); // 0..1
            x = x * 2.f - 1.f; // -1..1
            return x * (1.f - x * x * 0.3333f) * 4.f; // parabool approximatie
        };
        float wobble = wowDepth     * fast_sin(wowPhase)
                     + flutterDepth * fast_sin(flutterPhase);
        float tapeSamples = clamp(delaySamples * (1.f + wobble), 1.f, (float)(MAX_BUFFER - 1));

        int tapeReadPos = writePos - (int)tapeSamples;
        if (tapeReadPos < 0) tapeReadPos += MAX_BUFFER;
        float tapeSignal = buffer[tapeReadPos];

        // Lowpass — donkerder bij hogere tape
        float tapeAlpha = 0.04f + (1.f - tape) * 0.36f;
        tapeFilterState += tapeAlpha * (tapeSignal - tapeFilterState);

        // Saturatie
        // tapeCurve lineair — geen vroege piek in het midden
        // Lagere drive — minder schelle vervorming in het midden

        // ── LAAG 3: DROPOUTS (50→100%) ────────
        // Korte volume dips — versleten band gevoel
        dropoutTimer--;
        if (dropoutTimer <= 0 && damageStage > 0.f) {
            rngState = rngState * 1664525u + 1013904223u;
            float roll = (float)(rngState >> 16) / 65535.f;
            if (roll < damageStage * 0.012f) {
                rngState = rngState * 1664525u + 1013904223u;
                float dur = (float)(rngState >> 16) / 65535.f;
                // Dropout duur: 10-60ms
                dropoutTimer  = (int)((0.01f + dur * 0.05f) * args.sampleRate);
                // Diepte schaalt met damage — nooit volledig stil
                dropoutTarget = 1.f - (damageStage * 0.75f);
            } else {
                dropoutTimer = 50;
            }
        }
        dropoutGain += (dropoutTarget - dropoutGain) * 0.01f;
        if (dropoutGain > 0.995f) dropoutTarget = 1.f;



        // ── DRIVE — Tape saturatie + wavefold ─
        // Stap 1: Asymmetrische tape saturatie
        auto tapeSaturate = [](float x, float amt) -> float {
            // Positief en negatief anders behandeld — tape karakter
            if (x >= 0.f)
                return fast_tanh(x * amt) / amt;
            else
                return -fast_tanh(-x * amt * 0.8f) / (amt * 0.8f);
        };


        float driveAmt  = 1.f + drive * 2.0f;


        // ── STEREO SPREAD ─────────────────────
        float spread = clamp(params[SPREAD_PARAM].getValue()
            + (inputs[SPREAD_CV_INPUT].isConnected()
                ? inputs[SPREAD_CV_INPUT].getVoltage() * 0.1f : 0.f), 0.f, 1.f);
        spreadSmooth += (spread - spreadSmooth) * 0.01f;

        // Spread: L iets eerder, R iets later
        float spreadSamples = spreadSmooth * delaySamples * 0.25f;

        auto readSpread = [&](float offset) -> float {
            int pos = writePos - (int)clamp(offset, 1.f, (float)(MAX_BUFFER - 1));
            if (pos < 0) pos += MAX_BUFFER;
            return safeAudio(buffer[pos]);
        };

        // Wobble pitch effect — delayed leestijd schommelt
        float wobbleTimeL = delaySamples * (1.f + wobble * damageStage * 2.f);
        float wobbleTimeR = delaySamples * (1.f + wobble * damageStage * 2.f);
        float delayedL = readSpread(clamp(wobbleTimeL - spreadSamples, 1.f, (float)(MAX_BUFFER-1)));
        float delayedR = readSpread(clamp(wobbleTimeR + spreadSamples, 1.f, (float)(MAX_BUFFER-1)));

        // Tape blend op L en R
        // Tape karakter toepassen op delayedL/R — flutter, saturatie en hiss
        // Wet = delayed altijd volledig + tape karakter mengt erbij
        float wetL = delayedL;
        float wetR = delayedR;

        // Tape wobble — duidelijke modulatie
        float wobbleMod = 1.f + wobble * 3.f;
        wetL *= wobbleMod;
        wetR *= wobbleMod;

        // Dropouts + makeup gain bij hoge tape
        float tapeMakeup = 1.f + tape * 0.6f;
        wetL *= dropoutGain * tapeMakeup;
        wetR *= dropoutGain * tapeMakeup;

        // Hiss erbovenop — hoger na 50%
        float hissLvl = tape < 0.5f ? tape * 0.1f : 0.05f + (tape - 0.5f) * 0.6f;
        wetL += hissHigh * hissLvl;
        wetR += hissHigh * hissLvl;

        // Drive op wet
        float makeupGain = 1.f + clamp((drive - 0.75f) / 0.25f, 0.f, 1.f) * 0.8f;
        float wetDrivenL = tapeSaturate(wetL, driveAmt) * makeupGain;
        float wetDrivenR = tapeSaturate(wetR, driveAmt) * makeupGain;

        // ── MIX + OUTPUT ──────────────────────
        float dryGain = cosf(mix * M_PI * 0.5f);
        if (mix != lastMix) {
            cachedWetGain = sinf(mix * M_PI * 0.5f) * 1.3f;
            lastMix = mix;
        }
        float wetGain = cachedWetGain;

        float dryL = dry;
        float dryR = inputs[AUDIO_R_INPUT].isConnected() ? dryRawR : dry;

        // Brake: pitch op wet via readBufBrake, volume op alles
        float outL = (dryL * dryGain + wetDrivenL * wetGain) * brakeSpeedSmooth;
        float outR = (dryR * dryGain + wetDrivenR * wetGain) * brakeSpeedSmooth;

        // Surge bloom
        float bloom = delayed * freezeGain * mix * 1.3f;
        outL += bloom;
        outR += bloom;

        // Keep Chrono running behind Dry and only crossfade its output.
        outL += (dryL - outL) * dryFade;
        outR += (dryR - outR) * dryFade;

        outputs[AUDIO_L_OUTPUT].setVoltage(safeAudio(outL));
        outputs[AUDIO_R_OUTPUT].setVoltage(safeAudio(outR));
    }
};

// ─────────────────────────────────────────────
//  WIDGET
// ─────────────────────────────────────────────

struct ChronoSlider : SvgSlider {
    ChronoSlider() {
        setBackgroundSvg(Svg::load(asset::plugin(pluginInstance, "res/ChronoSliderBg.png")));
        setHandleSvg(Svg::load(asset::plugin(pluginInstance, "res/ChronoSliderHandle.png")));
        setHandlePosCentered(math::Vec(6.76f, 98.187f), math::Vec(6.76f, 0.f));
        horizontal = false;
    }
};

struct ChronoSliderStepped : SvgSlider {
    ChronoSliderStepped() {
        setBackgroundSvg(Svg::load(asset::plugin(pluginInstance, "res/ChronoSliderBg.png")));
        setHandleSvg(Svg::load(asset::plugin(pluginInstance, "res/ChronoSliderHandle.png")));
        setHandlePosCentered(math::Vec(6.76f, 98.187f), math::Vec(6.76f, 0.f));
        horizontal = false;
        snap = true;
    }
};

struct ChronoWidget : ModuleWidget {
    ChronoWidget(Chrono* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Chrono.png")));

        // ── Knoppen ──────────────────────────
        addParam(createParamCentered<ChronoSubmitKnobMedium>(mm2px(Vec(20.212f, 35.157f)), module, Chrono::TIME_PARAM));
        addParam(createParamCentered<ChronoSubmitKnobMedium>(mm2px(Vec(20.212f, 63.087f)), module, Chrono::FEEDBACK_PARAM));
        addParam(createParamCentered<ChronoSubmitKnobSmall>(mm2px(Vec(11.549f, 89.325f)), module, Chrono::DIVISION_PARAM));
        addParam(createParamCentered<ChronoSubmitKnobSmall>(mm2px(Vec(29.721f, 89.409f)), module, Chrono::SPACING_PARAM));
        addParam(createParamCentered<ChronoSubmitKnobSmall>(mm2px(Vec(48.061f, 89.409f)), module, Chrono::SPREAD_PARAM));

        // ── Sliders ───────────────────────────
        addParam(createParam<ChronoSlider>(mm2px(Vec(42.228f - 3.5f, 27.249f)), module, Chrono::MIX_PARAM));
        addParam(createParam<ChronoSlider>(mm2px(Vec(54.633f - 3.5f, 27.249f)), module, Chrono::DRIVE_PARAM));
        addParam(createParam<ChronoSlider>(mm2px(Vec(67.147f - 3.5f, 27.249f)), module, Chrono::TAPE_PARAM));
        addParam(createParam<ChronoSliderStepped>(mm2px(Vec(79.633f - 3.5f, 27.249f)), module, Chrono::HEADS_PARAM));

        // ── Momentary knoppen ─────────────────
        addParam(createParamCentered<ChronoSurgeButton>(mm2px(Vec(66.121f, 89.582f)), module, Chrono::SURGE_PARAM));
        addParam(createParamCentered<ChronoBreakButton>(mm2px(Vec(84.134f, 89.582f)), module, Chrono::BREAK_PARAM));

        // ── LEDs ──────────────────────────────
        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(60.558f, 79.530f)), module, Chrono::SURGE_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(80.297f, 79.530f)), module, Chrono::BREAK_LIGHT));

        // ── CV Inputs ─────────────────────────
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.051f, 25.908f)), module, Chrono::TIME_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.051f, 53.396f)), module, Chrono::FEEDBACK_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(41.978f, 68.461f)), module, Chrono::MIX_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(54.401f, 68.461f)), module, Chrono::DRIVE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(66.843f, 68.461f)), module, Chrono::TAPE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(79.826f, 68.461f)), module, Chrono::HEADS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.531f, 101.434f)), module, Chrono::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.721f, 101.434f)), module, Chrono::SPACING_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(47.811f, 101.434f)), module, Chrono::SPREAD_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(66.013f, 101.434f)), module, Chrono::SURGE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(84.199f, 101.434f)), module, Chrono::BREAK_CV_INPUT));

        // ── Audio I/O ─────────────────────────
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.632f, 116.192f)), module, Chrono::AUDIO_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(29.536f, 116.192f)), module, Chrono::AUDIO_R_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(47.811f, 116.192f)), module, Chrono::AUDIO_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(66.120f, 116.192f)), module, Chrono::AUDIO_R_OUTPUT));
    }
};

Model* modelChrono = createModel<Chrono, ChronoWidget>("Chrono");
