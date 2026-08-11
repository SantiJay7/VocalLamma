#include "plugin.hpp"

#include <array>

namespace {

static const float TAU = 6.283185307179586f;

/** Rosenberg-Klatt glottal pulse. `ph` is the phase in [0,1), `oq` the open
quotient (fraction of the period the glottis is open), `aq` the asymmetry
(fraction of the open phase spent opening). Blended with a sawtooth this gives
a voice-like, "monk" source with strong vowel formants. */
static inline float glottalPulse(float ph, float oq, float aq) {
	float op = oq * aq;
	float cq = oq * (1.f - aq);
	if (ph <= op) {
		float t = ph / op;
		return 3.f * t * t - 2.f * t * t * t;
	}
	else if (ph <= oq) {
		float t = (ph - op) / cq;
		float m = 1.f - t;
		return m * m;
	}
	return 0.f;
}

/** PolyBLEP residual for a sawtooth. `t` is the phase in [0,1], `dt` the phase increment. */
static inline float polyBlep(float t, float dt) {
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.f;
	}
	else if (t > 1.f - dt) {
		t = (t - 1.f) / dt;
		return t * t + t + t + 1.f;
	}
	return 0.f;
}

/** Biquad bandpass with constant 0 dB peak gain (RBJ cookbook). */
struct Bandpass {
	float b0 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
	float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;

	void set(float f, float bw, float sr) {
		float w0 = TAU * f / sr;
		float alpha = std::sin(w0) * bw / (2.f * f);
		float a0 = 1.f + alpha;
		b0 = alpha / a0;
		b2 = -alpha / a0;
		a1 = 2.f * std::cos(w0) / a0;
		a2 = (alpha - 1.f) / a0;
	}

	float process(float x) {
		float y = b0 * x + b2 * x2 + a1 * y1 + a2 * y2;
		x2 = x1;
		x1 = x;
		y2 = y1;
		y1 = y;
		return y;
	}

	void reset() {
		x1 = x2 = y1 = y2 = 0.f;
	}
};

/** Average-male formant frequencies (Hz) for OO-OH-AH-AY-EE. */
static const float VOWEL_FORMANTS[5][3] = {
	{300.f, 870.f, 2240.f},   // OO /u/
	{570.f, 840.f, 2410.f},   // OH /o/
	{730.f, 1090.f, 2440.f},  // AH /a/
	{530.f, 1840.f, 2480.f},  // AY /e/
	{270.f, 2290.f, 3010.f},  // EE /i/
};

/** Fixed formant bandwidths (Hz). */
static const float FORMANT_BW[3] = {60.f, 110.f, 160.f};

} // namespace


struct VocalLamma : Module {
	enum ParamIds {
		PITCH_PARAM,
		VOWEL_PARAM,
		VOWEL_ATT_PARAM,
		GLIDE_PARAM,
		VIBRATO_PARAM,
		VOICE_PARAM,
		FORMANT_PARAM,
		GATE_SWITCH_PARAM,
		TIME_PARAM,
		FEEDBACK_PARAM,
		MIX_PARAM,
		INPUT_MIX_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		VOWEL_INPUT,
		VIBRATO_INPUT,
		TIME_INPUT,
		EXT_INPUT,
		GATE_INPUT,
		INPUT_CV_INPUT,
		FORMANT_CV_INPUT,
		GLIDE_CV_INPUT,
		GATE_TRIG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		LEFT_OUTPUT,
		RIGHT_OUTPUT,
		PITCH_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		VOICE_LIGHT,
		GATE_LIGHT,
		NUM_LIGHTS
	};

	// Vocal synth state
	float pitchLog = 5.f;
	float vibratoPhase = 0.f;
	float vowelSmoothed = 0.5f;
	float phase = 0.f;
	float voiceLevel = 0.f;
	float gateLevel = 1.f;
	float gateTrigPrev = 0.f;
	float pitchJitter = 0.f;
	float oqPhase = 0.f;
	Bandpass formant[3];
	Bandpass noiseFormant[3];

	// Stereo delay state
	static const int DELAY_BUFFER_SIZE = 1 << 19;
	std::array<float, DELAY_BUFFER_SIZE> delayL{};
	std::array<float, DELAY_BUFFER_SIZE> delayR{};
	int delayIndex = 0;
	float delayTime = 0.2f;

	// MIDI state
	midi::InputQueue midiInput;
	std::vector<uint8_t> midiNotes;
	float midiBend = 0.f;
	float midiVibrato = 0.f;
	float midiMix = 0.f;
	float midiFormant = 0.f;

	VocalLamma() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(PITCH_PARAM, 0.f, 10.f, 5.f, "Pitch", " V/oct");
		configParam(VOWEL_PARAM, 0.f, 1.f, 0.5f, "Vowel");
		configParam(VOWEL_ATT_PARAM, -1.f, 1.f, 0.f, "Vowel CV amount");
		configParam(GLIDE_PARAM, 0.f, 5.f, 0.1f, "Glide", " s");
		configParam(VIBRATO_PARAM, 0.f, 1.f, 0.f, "Vibrato");
		configParam(VOICE_PARAM, 0.f, 1.f, 1.f, "Voice");
		configParam(FORMANT_PARAM, 0.f, 1.f, 0.5f, "Formant");
		configSwitch(GATE_SWITCH_PARAM, 0.f, 1.f, 0.f, "Gate on");
		configParam(TIME_PARAM, 0.f, 1.f, 0.3f, "Time", " s", 200.f, 0.01f);
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.5f, "Feedback");
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix");
		configParam(INPUT_MIX_PARAM, 0.f, 1.f, 1.f, "Input mix");

		configInput(PITCH_INPUT, "Pitch");
		configInput(VOWEL_INPUT, "Vowel");
		configInput(VIBRATO_INPUT, "Vibrato");
		configInput(TIME_INPUT, "Time");
		configInput(EXT_INPUT, "External audio");
		configInput(GATE_INPUT, "Gate");
		configInput(INPUT_CV_INPUT, "Input mix CV");
		configInput(FORMANT_CV_INPUT, "Formant CV");
		configInput(GLIDE_CV_INPUT, "Glide CV");
		configInput(GATE_TRIG_INPUT, "Gate trigger");

		configOutput(LEFT_OUTPUT, "Left");
		configOutput(RIGHT_OUTPUT, "Right");
		configOutput(PITCH_OUTPUT, "Voice pitch V/oct");
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		onSampleRateChange({APP->engine->getSampleRate(), 1.f / APP->engine->getSampleRate()});
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "midi", midiInput.toJson());
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* midiJ = json_object_get(rootJ, "midi");
		if (midiJ)
			midiInput.fromJson(midiJ);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		delayL.fill(0.f);
		delayR.fill(0.f);
		delayIndex = 0;
		delayTime = 0.2f;
		phase = 0.f;
		vibratoPhase = 0.f;
		voiceLevel = 0.f;
		gateLevel = 1.f;
		gateTrigPrev = 0.f;
		pitchJitter = 0.f;
		oqPhase = 0.f;
		midiNotes.clear();
		midiBend = 0.f;
		midiVibrato = 0.f;
		midiMix = 0.f;
		midiFormant = 0.f;
		for (int i = 0; i < 3; i++) {
			formant[i].reset();
			noiseFormant[i].reset();
		}
	}

	void removeMidiNote(uint8_t note) {
		for (auto it = midiNotes.begin(); it != midiNotes.end(); ++it) {
			if (*it == note) {
				midiNotes.erase(it);
				return;
			}
		}
	}

	static void getFormants(float v, float* out) {
		float x = clamp(v, 0.f, 1.f) * 4.f;
		int i = (int) x;
		if (i > 3)
			i = 3;
		float t = x - i;
		for (int k = 0; k < 3; k++)
			out[k] = VOWEL_FORMANTS[i][k] + (VOWEL_FORMANTS[i + 1][k] - VOWEL_FORMANTS[i][k]) * t;
	}

	void process(const ProcessArgs& args) override {
		float sr = args.sampleRate;

		// ---- MIDI input ----
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			switch (msg.getStatus()) {
				case 0x9:  // Note on
					if (msg.getValue() > 0) {
						midiNotes.push_back(msg.getNote());
						// Fresh glottal cycle per note for clean articulation
						phase = 0.f;
						vibratoPhase = 0.f;
					}
					else
						removeMidiNote(msg.getNote());
					break;
				case 0x8:  // Note off
					removeMidiNote(msg.getNote());
					break;
				case 0xE: {  // Pitch bend -> vowel (as in the original)
					int val = (msg.getNote() & 0x7f) | ((msg.getValue() & 0x7f) << 7);
					midiBend = (val - 8192) / 8192.f;
					break;
				}
				case 0xB:  // Control change
					if (msg.getNote() == 1)  // Mod wheel -> vibrato
						midiVibrato = msg.getValue() / 127.f;
					else if (msg.getNote() == 12)  // CC12 -> delay mix
						midiMix = (msg.getValue() - 64) / 127.f;
					else if (msg.getNote() == 13)  // CC13 -> formant character
						midiFormant = (msg.getValue() - 64) / 127.f;
					break;
			}
		}
		float midiPitchOct = midiNotes.empty() ? 0.f : (midiNotes.back() - 60) / 12.f;

		// ---- Pitch, glide and vibrato ----
		float jitterCoef = 1.f - std::exp(-1.f / (0.4f * sr));
		pitchJitter += (random::normal() - pitchJitter) * jitterCoef;
		float pitchVoct = params[PITCH_PARAM].getValue() + inputs[PITCH_INPUT].getVoltage() + midiPitchOct + pitchJitter * 0.08f;
		float glide = clamp(params[GLIDE_PARAM].getValue() + inputs[GLIDE_CV_INPUT].getVoltage() * 0.5f, 0.f, 5.f);
		float glideCoef = (glide > 0.f) ? 1.f - std::exp(-1.f / (glide * sr)) : 1.f;
		pitchLog += (pitchVoct - pitchLog) * glideCoef;
		outputs[PITCH_OUTPUT].setVoltage(pitchLog);

		float vibratoDepth = clamp(params[VIBRATO_PARAM].getValue() + inputs[VIBRATO_INPUT].getVoltage() / 10.f + midiVibrato, 0.f, 1.f);
		vibratoPhase += TAU * 5.5f / sr;
		if (vibratoPhase > TAU)
			vibratoPhase -= TAU;
		float vibrato = vibratoDepth * (0.5f / 12.f) * std::sin(vibratoPhase);

		float freq = 8.1758f * std::pow(2.f, pitchLog + vibrato);
		freq = clamp(freq, 10.f, 20000.f);

		// ---- Vowel -> formant frequencies ----
		float vowelTarget = clamp(params[VOWEL_PARAM].getValue() + params[VOWEL_ATT_PARAM].getValue() * inputs[VOWEL_INPUT].getVoltage() / 10.f + 0.5f * midiBend, 0.f, 1.f);
		float vowelCoef = 1.f - std::exp(-1.f / (0.02f * sr));
		vowelSmoothed += (vowelTarget - vowelSmoothed) * vowelCoef;
		float formantF[3];
		getFormants(vowelSmoothed, formantF);
		float formantScale = 0.7f + 0.6f * clamp(params[FORMANT_PARAM].getValue() + inputs[FORMANT_CV_INPUT].getVoltage() / 10.f + midiFormant, 0.f, 1.f);
		for (int i = 0; i < 3; i++)
			formantF[i] *= formantScale;

		// ---- Glottal source: saw + Rosenberg pulse blend ----
		float dt = freq / sr;
		phase += dt;
		if (phase >= 1.f)
			phase -= 1.f;
		// Slow open-quotient LFO: the glottis "breathes", keeping the voice alive
		oqPhase += TAU * 0.8f / sr;
		if (oqPhase > TAU)
			oqPhase -= TAU;
		float oq = 0.58f + 0.08f * std::sin(oqPhase);
		float saw = 2.f * phase - 1.f - polyBlep(phase, dt);
		float pulse = glottalPulse(phase, oq, 0.80f) - 0.2567f;
		float source = (0.6f * saw + 0.4f * pulse * 2.2f) * 1.7f;

		// ---- Parallel formant filters ----
		for (int i = 0; i < 3; i++)
			formant[i].set(formantF[i], FORMANT_BW[i], sr);
		float voice = formant[0].process(source);
		voice += 0.85f * formant[1].process(source);
		voice += 0.6f * formant[2].process(source);

		// ---- Breathiness: vowel-coloured noise through the same formants ----
		for (int i = 0; i < 3; i++)
			noiseFormant[i].set(formantF[i], FORMANT_BW[i], sr);
		float noiseIn = random::normal() * 0.9f;
		voice += noiseFormant[0].process(noiseIn);
		voice += 0.85f * noiseFormant[1].process(noiseIn);
		voice += 0.6f * noiseFormant[2].process(noiseIn);

		voice *= params[VOICE_PARAM].getValue();

		// ---- Gate trigger: toggle the GATE ON latch on a rising edge ----
		float gateTrig = inputs[GATE_TRIG_INPUT].getVoltage();
		if (gateTrig > 1.f && gateTrigPrev <= 1.f) {
			float cur = params[GATE_SWITCH_PARAM].getValue();
			params[GATE_SWITCH_PARAM].setValue(cur > 0.5f ? 0.f : 1.f);
		}
		gateTrigPrev = gateTrig;

		// ---- Gate: switch enables gate mode, voice sounds while gated ----
		float gateTarget = 1.f;
		if (params[GATE_SWITCH_PARAM].getValue() > 0.5f) {
			bool gateHigh = (inputs[GATE_INPUT].isConnected() && inputs[GATE_INPUT].getVoltage() > 1.f) || !midiNotes.empty();
			gateTarget = gateHigh ? 1.f : 0.f;
		}
		float gateTime = (gateTarget > gateLevel) ? 0.003f : 0.02f;
		gateLevel += (gateTarget - gateLevel) * (1.f - std::exp(-1.f / (gateTime * sr)));
		voice *= gateLevel;
		lights[GATE_LIGHT].setBrightness(params[GATE_SWITCH_PARAM].getValue() > 0.5f ? 1.f : 0.f);

		voice = std::tanh(1.5f * voice) * 3.5f;

		// ---- Stereo ping-pong delay ----
		float timeVal = clamp(params[TIME_PARAM].getValue() + inputs[TIME_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		float timeTarget = 0.01f * std::pow(200.f, timeVal);
		float maxTime = (float) DELAY_BUFFER_SIZE / sr - 0.001f;
		timeTarget = clamp(timeTarget, 0.005f, maxTime);
		float timeCoef = 1.f - std::exp(-1.f / (0.05f * sr));
		delayTime += (timeTarget - delayTime) * timeCoef;

		float delaySamples = delayTime * sr;
		int iDelay = (int) delaySamples;
		float frac = delaySamples - iDelay;

		int readPos = delayIndex - iDelay - 1;
		readPos %= DELAY_BUFFER_SIZE;
		if (readPos < 0)
			readPos += DELAY_BUFFER_SIZE;
		int readPos2 = readPos + 1;
		if (readPos2 >= DELAY_BUFFER_SIZE)
			readPos2 = 0;

		float wl = delayL[readPos] * (1.f - frac) + delayL[readPos2] * frac;
		float wr = delayR[readPos] * (1.f - frac) + delayR[readPos2] * frac;

		float fb = params[FEEDBACK_PARAM].getValue();
		float extMix = clamp(params[INPUT_MIX_PARAM].getValue() + inputs[INPUT_CV_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		float ext = inputs[EXT_INPUT].getVoltage() * extMix;
		float dry = voice + ext;

		delayL[delayIndex] = dry + wr * fb;
		delayR[delayIndex] = dry + wl * fb;
		delayIndex++;
		if (delayIndex >= DELAY_BUFFER_SIZE)
			delayIndex = 0;

		float mix = clamp(params[MIX_PARAM].getValue() + midiMix, 0.f, 1.f);
		float outL = dry * (1.f - mix) + wl * mix;
		float outR = dry * (1.f - mix) + wr * mix;
		outL = 10.f * std::tanh(outL / 10.f);
		outR = 10.f * std::tanh(outR / 10.f);

		outputs[LEFT_OUTPUT].setVoltage(outL);
		outputs[RIGHT_OUTPUT].setVoltage(outR);

		// ---- Activity light (also shows gate state in gate mode) ----
		float levelCoef = 1.f - std::exp(-1.f / (0.01f * sr));
		voiceLevel += (std::fabs(voice) - voiceLevel) * levelCoef;
		float led = voiceLevel * 3.f;
		if (params[GATE_SWITCH_PARAM].getValue() > 0.5f)
			led += gateLevel;
		lights[VOICE_LIGHT].setBrightness(clamp(led, 0.f, 1.f));
	}
};


/** Direct-draw text label. `ui::Label` does not render inside this module's
framebuffer, so we draw text ourselves with the window's UI font. */
struct TextLabel : Widget {
	std::string text;
	float fontSize;
	NVGcolor color;

	TextLabel(std::string t, float fs, NVGcolor c) : text(t), fontSize(fs), color(c) {}

	void draw(const DrawArgs& args) override {
		if (!APP->window->uiFont) return;
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgFontSize(args.vg, fontSize);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, color);
		nvgText(args.vg, 0, 0, text.c_str(), NULL);
	}
};


/** Raster logo drawn with nanoVG. The panel SVG cannot embed raster images
(the bundled nanoSVG ignores <image> elements), so we paint the PNG ourselves.
The image is loaded in draw() from a stored path, since cached Image handles
become invalid when a DAW editor window is reopened (see Migrate2 guide). */
struct LogoWidget : Widget {
	std::string imagePath;

	void draw(const DrawArgs& args) override {
		if (imagePath.empty())
			return;
		std::shared_ptr<window::Image> image = APP->window->loadImage(imagePath);
		if (!image || image->handle < 0)
			return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		NVGpaint paint = nvgImagePattern(args.vg, 0, 0, box.size.x, box.size.y, 0.f, image->handle, 1.f);
		nvgFillPaint(args.vg, paint);
		nvgFill(args.vg);
	}
};


struct VocalLammaWidget : ModuleWidget {
	VocalLammaWidget(VocalLamma* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/VocalLamma.svg")));

		// Llama logo (replaces the old diamond motif; panel SVG cannot hold raster images)
		{
			LogoWidget* logo = new LogoWidget;
			logo->imagePath = asset::plugin(pluginInstance, "res/VocalLammaLogo.png");
			logo->box.pos = Vec(132.f - 13.f, 37.f);
			logo->box.size = Vec(26.f, 25.8f);
			addChild(logo);
		}

		// Vocal knobs
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12, 27)), module, VocalLamma::PITCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(76.5, 27)), module, VocalLamma::VOWEL_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(12, 45)), module, VocalLamma::GLIDE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(33.5, 45)), module, VocalLamma::VIBRATO_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(55, 45)), module, VocalLamma::FORMANT_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(76.5, 45)), module, VocalLamma::VOWEL_ATT_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(12, 58)), module, VocalLamma::VOICE_PARAM));

		// Gate button (latching, illuminated) next to the VOWEL knob
		addParam(createParamCentered<VCVBezelLatch>(mm2px(Vec(55, 27)), module, VocalLamma::GATE_SWITCH_PARAM));
		addChild(createLightCentered<VCVBezelLight<GreenLight>>(mm2px(Vec(55, 27)), module, VocalLamma::GATE_LIGHT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(55, 69)), module, VocalLamma::GATE_INPUT));

		// Vocal inputs
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(12, 69)), module, VocalLamma::PITCH_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(33.5, 69)), module, VocalLamma::GLIDE_CV_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(33.5, 58)), module, VocalLamma::VIBRATO_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(55, 58)), module, VocalLamma::FORMANT_CV_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(76.5, 58)), module, VocalLamma::VOWEL_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(76.5, 69)), module, VocalLamma::GATE_TRIG_INPUT));

		// Delay knobs
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12, 96)), module, VocalLamma::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(33.5, 96)), module, VocalLamma::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(55, 96)), module, VocalLamma::MIX_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(55, 108.5)), module, VocalLamma::INPUT_MIX_PARAM));

		// Delay inputs
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(12, 108.5)), module, VocalLamma::TIME_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(33.5, 108.5)), module, VocalLamma::EXT_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(76.5, 108.5)), module, VocalLamma::INPUT_CV_INPUT));

		// Outputs
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(55, 120.5)), module, VocalLamma::LEFT_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(76.5, 120.5)), module, VocalLamma::RIGHT_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(33.5, 120.5)), module, VocalLamma::PITCH_OUTPUT));

		// Activity light
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(12, 120.5)), module, VocalLamma::VOICE_LIGHT));

		// Text labels
		NVGcolor gold = nvgRGBA(0xe8, 0xb3, 0x4a, 0xff);
		NVGcolor dim = nvgRGBA(0x9a, 0x9a, 0xa8, 0xff);

		addLabel("VOCAL LAMMA", Vec(45.72, 9.2), 15.f, gold);
		addLabel("PITCH", Vec(12, 19), 7.f, dim);
		addLabel("GATE ON", Vec(55, 19), 6.f, dim);
		addLabel("VOWEL", Vec(76.5, 19), 7.f, gold);
		addLabel("OO OH AH AY EE", Vec(76.5, 15), 5.f, dim);
		addLabel("GLIDE", Vec(12, 37), 7.f, dim);
		addLabel("VIBRATO", Vec(33.5, 37), 7.f, dim);
		addLabel("FORMANT", Vec(55, 37), 6.5f, dim);
		addLabel("VOW ATT", Vec(76.5, 37), 6.f, dim);
		addLabel("VOICE", Vec(12, 51), 6.5f, dim);
		addLabel("VIBR CV", Vec(33.5, 51), 6.5f, dim);
		addLabel("FORMANT CV", Vec(55, 51), 6.f, dim);
		addLabel("VOWEL CV", Vec(76.5, 51), 6.5f, dim);
		addLabel("PITCH CV", Vec(12, 63.5), 6.5f, dim);
		addLabel("GLIDE CV", Vec(33.5, 63.5), 6.f, dim);
		addLabel("GATE", Vec(55, 63.5), 6.f, dim);
		addLabel("GATE TRIG", Vec(76.5, 63.5), 6.f, dim);
		addLabel("DELAY", Vec(45.72, 81), 9.f, gold);
		addLabel("TIME", Vec(12, 88), 7.f, dim);
		addLabel("FEEDBACK", Vec(33.5, 88), 7.f, dim);
		addLabel("MIX", Vec(55, 88), 7.f, dim);
		addLabel("INPUT MIX", Vec(55, 102.3), 6.f, dim);
		addLabel("TIME CV", Vec(12, 102.3), 6.f, dim);
		addLabel("EXT IN", Vec(33.5, 102.3), 6.f, dim);
		addLabel("INPUT CV", Vec(76.5, 102.3), 6.f, dim);
		addLabel("LEFT", Vec(55, 114.5), 5.5f, dim);
		addLabel("PITCH OUT", Vec(33.5, 114.5), 5.5f, dim);
		addLabel("RIGHT", Vec(76.5, 114.5), 5.5f, dim);
	}

	void appendContextMenu(ui::Menu* menu) override {
		VocalLamma* module = dynamic_cast<VocalLamma*>(this->module);
		ModuleWidget::appendContextMenu(menu);
		if (module) {
			menu->addChild(new ui::MenuSeparator);
			app::appendMidiMenu(menu, &module->midiInput);
		}
	}

	void addLabel(const std::string& text, math::Vec posMm, float fontSize, NVGcolor color) {
		TextLabel* label = new TextLabel(text, fontSize, color);
		label->box.pos = mm2px(posMm);
		addChild(label);
	}
};


Model* modelVocalLamma = createModel<VocalLamma, VocalLammaWidget>("VocalLamma");
