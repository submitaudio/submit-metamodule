#include "plugin.hpp"

#include <cmath>
#include <cstdio>

struct SyncSubmitKnobCompact : SvgKnob {
	SyncSubmitKnobCompact() {
		minAngle = -0.83 * M_PI;
		maxAngle = 0.83 * M_PI;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/SubmitKnobCompact.png")));
		shadow->opacity = 0.f;
	}
};

struct Sync : Module {
	enum ParamId {
		BPM_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		EXT_INPUT,
		RST_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		CLOCK_OUTPUT,
		DIV2_OUTPUT,
		MULT2_OUTPUT,
		RESET_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		RUN_LIGHT,
		EXT_LIGHT,
		CLOCK_LIGHT,
		RESET_LIGHT,
		LIGHTS_LEN
	};

	dsp::SchmittTrigger runTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger resetInputTrigger;
	dsp::SchmittTrigger externalTrigger;
	dsp::PulseGenerator clockPulse;
	dsp::PulseGenerator div2Pulse;
	dsp::PulseGenerator mult2Pulse;
	dsp::PulseGenerator resetPulse;
	dsp::PulseGenerator clockLightPulse;

	bool running = true;
	bool wasExternal = false;
	double phase = 0.0;
	int div2Counter = 0;
	int64_t samplesSinceExternal = 0;
	int64_t externalPeriod = 0;
	int64_t mult2Countdown = -1;
	float measuredBpm = 0.f;
	bool haveExternalEdge = false;
	bool externalTempoValid = false;

	Sync() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BPM_PARAM, 30.f, 300.f, 120.f, "Tempo", " BPM");
		getParamQuantity(BPM_PARAM)->snapEnabled = true;
		configButton(RUN_PARAM, "Run");
		configButton(RESET_PARAM, "Reset phase");
		configInput(EXT_INPUT, "External clock");
		configInput(RST_INPUT, "Reset");
		configOutput(CLOCK_OUTPUT, "Clock (x1)");
		configOutput(DIV2_OUTPUT, "Half-speed clock");
		configOutput(MULT2_OUTPUT, "Double-speed clock");
		configOutput(RESET_OUTPUT, "Reset pulse");
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "running", json_boolean(running));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* runningJson = json_object_get(root, "running");
		if (runningJson)
			running = json_boolean_value(runningJson);
	}

	void resetTiming() {
		phase = 0.0;
		div2Counter = 0;
		mult2Countdown = -1;
		clockPulse.reset();
		div2Pulse.reset();
		mult2Pulse.reset();
	}

	void emitClock() {
		clockPulse.trigger(1e-3f);
		mult2Pulse.trigger(1e-3f);
		clockLightPulse.trigger(40e-3f);
		if (div2Counter == 0)
			div2Pulse.trigger(1e-3f);
		div2Counter ^= 1;
	}

	void process(const ProcessArgs& args) override {
		if (runTrigger.process(params[RUN_PARAM].getValue())) {
			running = !running;
			resetTiming();
		}

		const bool resetPressed = resetTrigger.process(params[RESET_PARAM].getValue());
		const bool resetReceived = resetInputTrigger.process(inputs[RST_INPUT].getVoltage());
		if (resetPressed || resetReceived) {
			resetTiming();
			resetPulse.trigger(10e-3f);
		}

		const bool external = inputs[EXT_INPUT].isConnected();
		if (external != wasExternal) {
			resetTiming();
			externalTrigger.reset();
			samplesSinceExternal = 0;
			externalPeriod = 0;
			measuredBpm = 0.f;
			haveExternalEdge = false;
			externalTempoValid = false;
			wasExternal = external;
		}

		if (external) {
			++samplesSinceExternal;
			if (externalTrigger.process(inputs[EXT_INPUT].getVoltage())) {
				if (haveExternalEdge && samplesSinceExternal > 1) {
					externalPeriod = samplesSinceExternal;
					const float newBpm = args.sampleRate * 60.f / static_cast<float>(externalPeriod);
					if (newBpm >= 20.f && newBpm <= 400.f) {
						measuredBpm = externalTempoValid
							? measuredBpm + 0.2f * (newBpm - measuredBpm)
							: newBpm;
						externalTempoValid = true;
					}
				}
				haveExternalEdge = true;
				samplesSinceExternal = 0;

				if (running) {
					emitClock();
					if (externalPeriod > 2)
						mult2Countdown = externalPeriod / 2;
				}
			}

			if (running && mult2Countdown >= 0) {
				if (mult2Countdown == 0) {
					mult2Pulse.trigger(1e-3f);
					mult2Countdown = -1;
				} else {
					--mult2Countdown;
				}
			}

			if (externalPeriod > 0 && samplesSinceExternal > externalPeriod * 8) {
				externalTempoValid = false;
				measuredBpm = 0.f;
			}
		} else if (running) {
			const double pulseRate = params[BPM_PARAM].getValue() / 60.0;
			phase += pulseRate * args.sampleTime;

			if (phase >= 0.5 && phase - pulseRate * args.sampleTime < 0.5)
				mult2Pulse.trigger(1e-3f);

			if (phase >= 1.0) {
				phase -= std::floor(phase);
				emitClock();
			}
		}

		outputs[CLOCK_OUTPUT].setVoltage(clockPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[DIV2_OUTPUT].setVoltage(div2Pulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[MULT2_OUTPUT].setVoltage(mult2Pulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[RESET_OUTPUT].setVoltage(resetPulse.process(args.sampleTime) ? 10.f : 0.f);

		lights[RUN_LIGHT].setSmoothBrightness(running ? 1.f : 0.f, args.sampleTime);
		lights[EXT_LIGHT].setSmoothBrightness(external ? 1.f : 0.f, args.sampleTime);
		lights[CLOCK_LIGHT].setSmoothBrightness(clockLightPulse.process(args.sampleTime) ? 1.f : 0.f, args.sampleTime);
		lights[RESET_LIGHT].setSmoothBrightness(outputs[RESET_OUTPUT].getVoltage() > 1.f ? 1.f : 0.f, args.sampleTime);
	}
};

struct SyncDisplay : TransparentWidget {
	Sync* module = nullptr;
	std::shared_ptr<window::Font> font;

	SyncDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/ShareTechMono-Regular.ttf"));
	}

	void draw(const DrawArgs& args) override {
		nvgFontFaceId(args.vg, font ? font->handle : APP->window->uiFont->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(255, 255, 255));

		char value[16] = "120";
		bool external = false;
		if (module) {
			external = module->inputs[Sync::EXT_INPUT].isConnected();
			if (external) {
				if (module->externalTempoValid)
					snprintf(value, sizeof(value), "%.0f", module->measuredBpm);
				else
					snprintf(value, sizeof(value), "---");
			} else {
				snprintf(value, sizeof(value), "%.0f", module->params[Sync::BPM_PARAM].getValue());
			}
		}

		const float centerX = box.size.x * 0.5f - 1.f;
		const float bpmY = box.size.y * 0.40f;
		const float modeY = box.size.y * 0.84f;
		float bpmBounds[4];
		float modeBounds[4];
		nvgFontSize(args.vg, 20.f);
		nvgTextBounds(args.vg, centerX, bpmY, value, nullptr, bpmBounds);
		nvgFontSize(args.vg, 8.f);
		nvgTextBounds(args.vg, centerX, modeY, "INT / EXT", nullptr, modeBounds);
		const float blockTop = std::fmin(bpmBounds[1], modeBounds[1]);
		const float blockBottom = std::fmax(bpmBounds[3], modeBounds[3]);
		// Centreer op het werkelijke groene LCD-vlak van Sync V2
		// (paneel-Y 38.223..70.919), niet op de kleinere components-hitbox.
		const float lcdCenterY = 1.087f;
		const float centerOffset = lcdCenterY - (blockTop + blockBottom) * 0.5f;

		nvgFontSize(args.vg, 20.f);
		nvgText(args.vg, centerX, bpmY + centerOffset, value, nullptr);

		nvgFontSize(args.vg, 8.f);
		nvgFillColor(args.vg, external ? nvgRGB(255, 255, 255) : nvgRGB(255, 255, 0));
		nvgText(args.vg, box.size.x * 0.31f - 1.f, modeY + centerOffset, "INT", nullptr);
		nvgFillColor(args.vg, nvgRGB(255, 255, 255));
		nvgText(args.vg, box.size.x * 0.50f - 1.f, modeY + centerOffset, "/", nullptr);
		nvgFillColor(args.vg, external ? nvgRGB(255, 255, 0) : nvgRGB(255, 255, 255));
		nvgText(args.vg, box.size.x * 0.72f - 1.f, modeY + centerOffset, "EXT", nullptr);
	}
};

struct SyncWidget : ModuleWidget {
	SyncWidget(Sync* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Sync.png")));

		// Exacte centra uit Panel-components-Sync-META-V2.svg. Het 5HP-frame
		// heeft 3.327 px horizontale middenruimte aan beide zijden.
		constexpr float panelXOffset = 3.327f;
		auto* display = createWidget<SyncDisplay>(Vec(10.786f + panelXOffset, 40.484f));
		display->box.size = Vec(47.037f, 29.523f);
		display->module = module;
		addChild(display);

		// Small optical correction: the compact artwork reads slightly right-heavy.
		const Vec tempoCenter(34.272f + panelXOffset - 2.5f, 122.310f);
		addParam(createParamCentered<SyncSubmitKnobCompact>(tempoCenter, module, Sync::BPM_PARAM));
		addParam(createParamCentered<LEDButton>(Vec(17.358f + panelXOffset, 186.312f), module, Sync::RUN_PARAM));
		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(17.358f + panelXOffset, 186.312f), module, Sync::RUN_LIGHT));
		addParam(createParamCentered<LEDButton>(Vec(50.696f + panelXOffset, 186.312f), module, Sync::RESET_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(Vec(50.696f + panelXOffset, 186.312f), module, Sync::RESET_LIGHT));

		addInput(createInputCentered<PJ301MPort>(Vec(18.267f + panelXOffset, 249.791f), module, Sync::EXT_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(50.671f + panelXOffset, 249.791f), module, Sync::RST_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(18.267f + panelXOffset, 302.037f), module, Sync::CLOCK_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(50.671f + panelXOffset, 302.037f), module, Sync::MULT2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(18.267f + panelXOffset, 342.001f), module, Sync::DIV2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(50.671f + panelXOffset, 342.001f), module, Sync::RESET_OUTPUT));
	}
};

Model* modelSync = createModel<Sync, SyncWidget>("Sync");
