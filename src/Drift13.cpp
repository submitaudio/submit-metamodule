#include "plugin.hpp"

struct Drift13 : Module {
    enum ParamId {
        PITCH_PARAM, FINE_PARAM, OVERTONE_PARAM, MULTIPLY_PARAM,
        RISE_PARAM, FALL_PARAM, TIME_PARAM, LOGEXP_PARAM, CYCLE_PARAM,
        ONSET_PARAM, SUSTAIN_PARAM, DECAY_PARAM, EXP_PARAM,
        BALANCE_PARAM, BALNC_PARAM, PARAMS_LEN
    };
    enum InputId {
        VOCT_INPUT, LINFM_INPUT, OVRTN_INPUT, MLTPL_INPUT,
        TRIG_INPUT, GATE_INPUT, SLOPE_INPUT, DCY_INPUT, CNTR_INPUT,
        DYNMC_INPUT, FVND_INPUT, OVRTN_BAL_INPUT, EXT_INPUT, TIMBRE_INPUT, INPUTS_LEN
    };
    enum OutputId {
        OUT1_OUTPUT, OUT2_OUTPUT, EOC_OUTPUT, EON_OUTPUT,
        CNTR_OUTPUT, CONTOUR_OUTPUT, LINEOUT_OUTPUT, OUTPUTS_LEN
    };
    enum LightId { CYCLE_LIGHT, ONSET_LIGHT, TIMBRE_LIGHT, LIGHTS_LEN };

    float phase=0.f;
    enum SlopeStage { IDLE, RISE, FALL };
    enum ContourStage { CONTOUR_IDLE, CONTOUR_ATTACK, CONTOUR_DECAY, CONTOUR_SUSTAIN, CONTOUR_RELEASE };
    SlopeStage slopeStage=IDLE;
    ContourStage contourStage=CONTOUR_IDLE;
    float slopeValue=0.f, slopeTime=0.f, slopeStartValue=0.f;
    float contourValue=0.f, contourTime=0.f, contourStartValue=0.f;
    float smoothedDynCV=0.f;
    float smoothedOvertone=0.f, smoothedMultiply=0.f, smoothedBalance=0.f;
    float lpgState1=0.f, lpgState2=0.f;
    float dcInput=0.f, dcOutput=0.f;
    bool lastGate=false, lastTrig=false, lastContourGate=false;
    float lastFallRaw=-1.f, lastTScale=-1.f, cachedFallT=0.001f;
    float lastOnset=-1.f, cachedAttackT=0.001f;
    float lastExp=-1.f, cachedContourCurve=0.f, cachedContourExponent=1.f;
    float lastSampleRate=0.f, fastSmoothCoeff=1.f, dynSmoothCoeff=1.f;
    float fallStartValue=1.f, lastExpAmt=-1.f, cachedExpFactor=1.f;
    bool oversample2x=false;
    dsp::PulseGenerator eocPulse, eonPulse, onsetPulse;

    Drift13() {
        config(PARAMS_LEN,INPUTS_LEN,OUTPUTS_LEN,LIGHTS_LEN);
        configParam(PITCH_PARAM,-4.f,4.f,-2.f,"Octave"," oct");
        paramQuantities[PITCH_PARAM]->snapEnabled=true;
        configParam(FINE_PARAM,-7.f,7.f,0.f,"Tune"," st");
        configParam(OVERTONE_PARAM,0.f,1.f,0.69639f,"Overtone");
        configParam(MULTIPLY_PARAM,0.f,1.f,0.74578f,"Multiply");
        configParam(RISE_PARAM,0.001f,0.5f,0.24328f,"Rise"," s");
        configParam(FALL_PARAM,0.001f,8.f,6.352f,"Fall"," s");
        configParam(TIME_PARAM,0.1f,4.f,3.1683f,"Time");
        configParam(LOGEXP_PARAM,-1.f,1.f,-0.6747f,"Curve");
        configSwitch(CYCLE_PARAM,0.f,1.f,0.f,"Cycle",{"Off","On"});
        configParam(ONSET_PARAM,0.f,1.f,0.f,"Onset");
        configParam(SUSTAIN_PARAM,0.f,1.f,0.33373f,"Sustain");
        configParam(DECAY_PARAM,0.16483f,3.f,0.4381f,"Decay"," s");
        configParam(EXP_PARAM,0.f,1.f,0.83976f,"Exp");
        configParam(BALANCE_PARAM,0.f,1.f,0.71687f,"Timbre");
        configSwitch(BALNC_PARAM,0.f,1.f,1.f,"Timbre",{"Off","On"});
        configInput(VOCT_INPUT,"V/OCT");
        configInput(LINFM_INPUT,"FM");
        configInput(OVRTN_INPUT,"OVR");
        configInput(MLTPL_INPUT,"MLT");
        configInput(TRIG_INPUT,"TRIG");
        configInput(GATE_INPUT,"GATE");
        configInput(SLOPE_INPUT,"SLP");
        configInput(DCY_INPUT,"DCY");
        configInput(CNTR_INPUT,"CTR");
        configInput(DYNMC_INPUT,"DYN");
        configInput(FVND_INPUT,"Fundamental CV");
        configInput(OVRTN_BAL_INPUT,"Overtone bal CV");
        configInput(EXT_INPUT,"Ext In");
        configInput(TIMBRE_INPUT,"Timbre CV");
        configOutput(OUT1_OUTPUT,"TRI");
        configOutput(OUT2_OUTPUT,"SQR");
        configOutput(EOC_OUTPUT,"EOC");
        configOutput(EON_OUTPUT,"EON");
        configOutput(CNTR_OUTPUT,"SLP");
        configOutput(CONTOUR_OUTPUT,"ENV");
        configOutput(LINEOUT_OUTPUT,"LINE OUT");
    }

    float waveFolder(float x, float amount) {
        amount=clamp(amount,0.f,1.f);
        if (amount<0.001f) return x;
        float bias=0.025f*amount;
        float drive=1.f+amount*4.5f;
        float y=(x+bias*(1.f-x*x))*drive;
        y=std::fmod(y+1.f,4.f);
        if (y<0.f) y+=4.f;
        if (y>2.f) y=4.f-y;
        y-=1.f;
        float saturation=1.f+amount*0.65f;
        float folded=std::tanh(y*saturation)/std::tanh(saturation);
        float blend=amount*amount*(3.f-2.f*amount);
        return x+(folded-x)*blend;
    }

    float polyBlep(float t, float dt) {
        if (t<dt) {
            t/=dt;
            return t+t-t*t-1.f;
        }
        if (t>1.f-dt) {
            t=(t-1.f)/dt;
            return t*t+t+t+1.f;
        }
        return 0.f;
    }

    float applySlopeCurve(float x, float curve) {
        x=clamp(x,0.f,1.f);
        if (std::abs(curve)<1e-4f) return x;
        float denom=curve-2.f*curve*std::abs(x)+1.f;
        if (std::abs(denom)<1e-6f) return x;
        return (x-curve*x)/denom;
    }

    float applyContourCurve(float x, float curve, float exponent) {
        x=clamp(x,0.f,1.f);
        if (std::abs(curve)<1e-4f) return x;
        return curve>0.f?std::pow(x,exponent):1.f-std::pow(1.f-x,exponent);
    }

    float smoothValue(float current, float target, float coefficient) {
        return current+(target-current)*coefficient;
    }

    float applyCurve(float x, float curve) {
        return applySlopeCurve(x,curve);
    }

    float overtoneShaper(float x, float amount) {
        if (amount<0.001f) return x;
        float shaped=std::tanh((x+amount*0.3f)*(1.f+amount*2.f));
        return x*(1.f-amount)+shaped*amount;
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate!=lastSampleRate) {
            fastSmoothCoeff=1.f-std::exp(-args.sampleTime/0.001f);
            dynSmoothCoeff=1.f-std::exp(-args.sampleTime/0.0015f);
            lastSampleRate=args.sampleRate;
        }

        bool gate=inputs[GATE_INPUT].getVoltage()>(lastGate?0.1f:1.f);
        bool trig=inputs[TRIG_INPUT].getVoltage()>(lastTrig?0.1f:1.f);
        bool cycle=params[CYCLE_PARAM].getValue()>0.5f;
        float tScale=params[TIME_PARAM].getValue();
        float riseControl;
        if (cycle)
            riseControl=params[RISE_PARAM].getValue();
        else if (inputs[TRIG_INPUT].isConnected())
            riseControl=0.001f+clamp(inputs[TRIG_INPUT].getVoltage()/10.f,0.f,1.f)*0.499f;
        else
            riseControl=0.5f;
        float riseT=riseControl*tScale;
        float fallRaw=params[FALL_PARAM].getValue();
        if (fallRaw!=lastFallRaw||tScale!=lastTScale) {
            cachedFallT=0.001f*std::pow(2000.f,fallRaw/8.f)*tScale;
            lastFallRaw=fallRaw;
            lastTScale=tScale;
        }
        float slopeCV=inputs[SLOPE_INPUT].isConnected()?inputs[SLOPE_INPUT].getVoltage()/5.f:0.f;
        float logexp=clamp(params[LOGEXP_PARAM].getValue()+slopeCV,-1.f,1.f);

        if ((!cycle&&gate&&!lastGate)||(cycle&&trig&&!lastTrig)) {
            slopeStartValue=slopeValue;
            slopeStage=RISE;
            slopeTime=0.f;
            onsetPulse.trigger(0.05f);
        }
        if (cycle&&slopeStage==IDLE) {
            slopeStartValue=slopeValue;
            slopeStage=RISE;
            slopeTime=0.f;
        }
        if (slopeStage==RISE) {
            slopeTime+=args.sampleTime;
            float t=clamp(slopeTime/riseT,0.f,1.f);
            slopeValue=slopeStartValue+(1.f-slopeStartValue)*applySlopeCurve(t,logexp);
            if (t>=1.f) {
                slopeValue=1.f;
                eonPulse.trigger(1e-3f);
                slopeStartValue=1.f;
                slopeStage=FALL;
                slopeTime=0.f;
            }
        } else if (slopeStage==FALL) {
            slopeTime+=args.sampleTime;
            float t=clamp(slopeTime/cachedFallT,0.f,1.f);
            slopeValue=slopeStartValue*(1.f-applySlopeCurve(t,-logexp));
            if (t>=1.f) {
                slopeValue=0.f;
                eocPulse.trigger(1e-3f);
                if (cycle) {slopeStartValue=0.f;slopeStage=RISE;slopeTime=0.f;}
                else slopeStage=IDLE;
            }
        }

        lastGate=gate;
        lastTrig=trig;
        outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime)?10.f:0.f);
        outputs[EON_OUTPUT].setVoltage(eonPulse.process(args.sampleTime)?10.f:0.f);
        outputs[CNTR_OUTPUT].setVoltage(slopeValue*10.f);
        lights[CYCLE_LIGHT].setBrightness(cycle?slopeValue:0.f);
        lights[TIMBRE_LIGHT].setBrightness(params[BALNC_PARAM].getValue());
        lights[ONSET_LIGHT].setBrightness(onsetPulse.process(args.sampleTime)?1.f:0.f);

        bool contourGate=inputs[CNTR_INPUT].isConnected()
            ? inputs[CNTR_INPUT].getVoltage()>(lastContourGate?0.1f:1.f)
            : (inputs[GATE_INPUT].isConnected()?gate:(slopeStage!=IDLE));
        float onset=params[ONSET_PARAM].getValue();
        if (onset!=lastOnset) {
            cachedAttackT=0.001f*std::pow(2000.f,onset);
            lastOnset=onset;
        }
        float decayCV=inputs[DCY_INPUT].isConnected()?inputs[DCY_INPUT].getVoltage()/5.f:0.f;
        float decayT=clamp(params[DECAY_PARAM].getValue()+decayCV,0.16483f,3.f);
        float sustain=clamp(params[SUSTAIN_PARAM].getValue(),0.f,1.f);
        float expParam=params[EXP_PARAM].getValue();
        if (expParam!=lastExp) {
            cachedContourCurve=clamp(expParam*1.8f-0.9f,-0.9f,0.9f);
            cachedContourExponent=std::pow(5.f,std::abs(cachedContourCurve));
            lastExp=expParam;
        }

        if (contourGate&&!lastContourGate) {
            contourStartValue=contourValue;
            contourTime=0.f;
            contourStage=CONTOUR_ATTACK;
        } else if (!contourGate&&lastContourGate&&contourStage!=CONTOUR_IDLE) {
            contourStartValue=contourValue;
            contourTime=0.f;
            contourStage=CONTOUR_RELEASE;
        }
        lastContourGate=contourGate;

        if (contourStage==CONTOUR_ATTACK) {
            contourTime+=args.sampleTime;
            float t=clamp(contourTime/cachedAttackT,0.f,1.f);
            contourValue=contourStartValue+(1.f-contourStartValue)*applyContourCurve(t,cachedContourCurve,cachedContourExponent);
            if (t>=1.f) {contourValue=1.f;contourStartValue=1.f;contourTime=0.f;contourStage=CONTOUR_DECAY;}
        } else if (contourStage==CONTOUR_DECAY) {
            contourTime+=args.sampleTime;
            float t=clamp(contourTime/decayT,0.f,1.f);
            contourValue=contourStartValue+(sustain-contourStartValue)*applyContourCurve(t,-cachedContourCurve,cachedContourExponent);
            if (t>=1.f) {contourValue=sustain;contourStage=contourGate?CONTOUR_SUSTAIN:CONTOUR_RELEASE;contourStartValue=contourValue;contourTime=0.f;}
        } else if (contourStage==CONTOUR_SUSTAIN) {
            contourValue=sustain;
        } else if (contourStage==CONTOUR_RELEASE) {
            contourTime+=args.sampleTime;
            float t=clamp(contourTime/decayT,0.f,1.f);
            contourValue=contourStartValue*(1.f-applyContourCurve(t,-cachedContourCurve,cachedContourExponent));
            if (t>=1.f) {contourValue=0.f;contourStage=CONTOUR_IDLE;}
        }
        outputs[CONTOUR_OUTPUT].setVoltage(contourValue*10.f);

        float dynCV=inputs[DYNMC_INPUT].isConnected()?clamp(inputs[DYNMC_INPUT].getVoltage()/10.f,0.f,1.f):contourValue;
        smoothedDynCV=smoothValue(smoothedDynCV,clamp(dynCV,0.f,1.f),dynSmoothCoeff);

        float overtoneTarget=clamp(params[OVERTONE_PARAM].getValue()+(inputs[OVRTN_INPUT].isConnected()?inputs[OVRTN_INPUT].getVoltage()/10.f:0.f),0.f,1.f);
        float multiplyPanel=params[MULTIPLY_PARAM].getValue();
        float multiplyTarget;
        if (inputs[MLTPL_INPUT].isConnected())
            multiplyTarget=clamp(multiplyPanel+inputs[MLTPL_INPUT].getVoltage()/10.f,0.f,1.f);
        else if (cycle)
            multiplyTarget=clamp(multiplyPanel+(slopeValue-0.5f)*0.35f,0.f,1.f);
        else
            multiplyTarget=multiplyPanel;
        float balanceTarget=clamp(params[BALANCE_PARAM].getValue()+(inputs[TIMBRE_INPUT].isConnected()?inputs[TIMBRE_INPUT].getVoltage()/5.f:0.f),0.f,1.f);
        smoothedOvertone=smoothValue(smoothedOvertone,overtoneTarget,fastSmoothCoeff);
        smoothedMultiply=smoothValue(smoothedMultiply,multiplyTarget,fastSmoothCoeff);
        smoothedBalance=smoothValue(smoothedBalance,balanceTarget,fastSmoothCoeff);

        float pitchV=params[PITCH_PARAM].getValue()+params[FINE_PARAM].getValue()/12.f;
        if (inputs[VOCT_INPUT].isConnected()) pitchV+=inputs[VOCT_INPUT].getVoltage();
        if (inputs[LINFM_INPUT].isConnected()) pitchV+=inputs[LINFM_INPUT].getVoltage()*0.1f;
        float freq=clamp(dsp::FREQ_C4*std::pow(2.f,pitchV),1.f,20000.f);
        int oversample=oversample2x?2:1;
        float dt=clamp(freq*args.sampleTime/(float)oversample,1e-6f,0.49f);
        float triangleCore=0.f, square=0.f, complexSound=0.f;
        for (int i=0;i<oversample;++i) {
            phase+=dt;
            if (phase>=1.f) phase-=1.f;
            float tri=(phase<0.5f)?(4.f*phase-1.f):(3.f-4.f*phase);
            float sineCore=-std::cos(2.f*M_PI*phase);
            float subTriangle=tri*0.92f+sineCore*0.08f;
            float subSquare=phase<0.5f?1.f:-1.f;
            subSquare+=polyBlep(phase,dt);
            float shifted=phase+0.5f;
            if (shifted>=1.f) shifted-=1.f;
            subSquare-=polyBlep(shifted,dt);

            float harmonic=subTriangle*0.68f+subSquare*0.32f;
            float slopeAudio=(slopeStage!=IDLE||cycle)?slopeValue*2.f-1.f:subSquare;
            float overtoneSound;
            if (smoothedOvertone<0.5f)
                overtoneSound=subTriangle+(harmonic-subTriangle)*(smoothedOvertone*2.f);
            else
                overtoneSound=harmonic+(harmonic*0.7f+slopeAudio*0.3f-harmonic)*((smoothedOvertone-0.5f)*2.f);
            triangleCore+=subTriangle;
            square+=subSquare;
            complexSound+=waveFolder(overtoneSound,smoothedMultiply);
        }
        float invOversample=1.f/(float)oversample;
        triangleCore*=invOversample;
        square*=invOversample;
        complexSound*=invOversample;
        outputs[OUT1_OUTPUT].setVoltage(triangleCore*5.f);
        outputs[OUT2_OUTPUT].setVoltage(square*5.f);

        bool timbreOn=params[BALNC_PARAM].getValue()>0.5f;
        float mix=timbreOn?smoothedBalance:0.f;
        float voice=triangleCore+(complexSound-triangleCore)*mix;
        float voiceDrive=1.f+0.35f*mix;
        voice=std::tanh(voice*voiceDrive)/std::tanh(voiceDrive);

        float cutoff=45.f+std::pow(smoothedDynCV,1.7f)*18000.f;
        float lpgCoeff=1.f-std::exp(-2.f*M_PI*cutoff*args.sampleTime);
        lpgState1+=(voice-lpgState1)*lpgCoeff;
        lpgState2+=(lpgState1-lpgState2)*lpgCoeff;
        float lpgTone=lpgState1*0.72f+lpgState2*0.28f;
        float gain=std::pow(smoothedDynCV,1.25f);
        float out=std::tanh(lpgTone*gain*1.15f);

        float dcBlocked=out-dcInput+0.995f*dcOutput;
        dcInput=out;
        dcOutput=dcBlocked;
        outputs[LINEOUT_OUTPUT].setVoltage(dcBlocked*5.f);
    }

    void processLegacy(const ProcessArgs& args) {
        float pitchV=params[PITCH_PARAM].getValue()+params[FINE_PARAM].getValue()/12.f;
        if (inputs[VOCT_INPUT].isConnected()) pitchV+=inputs[VOCT_INPUT].getVoltage();
        if (inputs[LINFM_INPUT].isConnected()) pitchV+=inputs[LINFM_INPUT].getVoltage()*0.1f;
        float freq=clamp(dsp::FREQ_C4*std::pow(2.f,pitchV),1.f,20000.f);
        phase+=freq*args.sampleTime;
        if (phase>=1.f) phase-=1.f;
        float tri=(phase<0.5f)?(4.f*phase-1.f):(3.f-4.f*phase);
        float square=(tri>0.f)?1.f:-1.f;
        outputs[OUT1_OUTPUT].setVoltage(tri*5.f);
        outputs[OUT2_OUTPUT].setVoltage(square*5.f);

        float ovrAmt=clamp(params[OVERTONE_PARAM].getValue()+(inputs[OVRTN_INPUT].isConnected()?inputs[OVRTN_INPUT].getVoltage()/10.f:0.f),0.f,1.f);
        float shaped=overtoneShaper(tri,ovrAmt);

        bool gate=inputs[GATE_INPUT].getVoltage()>1.f;
        bool trig=inputs[TRIG_INPUT].getVoltage()>1.f;
        bool cycle=params[CYCLE_PARAM].getValue()>0.5f;
        if ((gate&&!lastGate)||(trig&&!lastTrig)){slopeStage=RISE;slopeTime=0.f;onsetPulse.trigger(0.05f);}
        if (inputs[GATE_INPUT].isConnected()&&!gate&&lastGate&&(slopeStage==RISE||slopeValue>0.f)){fallStartValue=slopeValue;slopeStage=FALL;slopeTime=0.f;}
        lastGate=gate; lastTrig=trig;
        float tScale=params[TIME_PARAM].getValue();
        float riseT=params[RISE_PARAM].getValue()*tScale;
        float fallRaw=params[FALL_PARAM].getValue();
        if (fallRaw != lastFallRaw || tScale != lastTScale) {
            cachedFallT = 0.001f * std::pow(8000.f, fallRaw / 8.f) * tScale;
            lastFallRaw = fallRaw;
            lastTScale = tScale;
        }
        float fallT = cachedFallT;
        float logexp=clamp(params[LOGEXP_PARAM].getValue()+(inputs[SLOPE_INPUT].isConnected()?inputs[SLOPE_INPUT].getVoltage()/5.f:0.f),-1.f,1.f);
        if (slopeStage==RISE){
            slopeTime+=args.sampleTime;
            float t=clamp(slopeTime/riseT,0.f,1.f);
            slopeValue=applyCurve(t,logexp);
            if (t>=1.f){slopeValue=1.f;slopeStage=FALL;slopeTime=0.f;}
        } else if (slopeStage==FALL){
            slopeTime+=args.sampleTime;
            float t=clamp(slopeTime/fallT,0.f,1.f);
            slopeValue=fallStartValue*(1.f-applyCurve(t,-logexp));
            if (t>=1.f){eocPulse.trigger(1e-3f);if(!gate)eonPulse.trigger(1e-3f);if(cycle){slopeStage=RISE;slopeTime=0.f;}else{slopeStage=IDLE;slopeValue=0.f;}}
        } else if (cycle){slopeStage=RISE;slopeTime=0.f;}
        outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime)?10.f:0.f);
        outputs[EON_OUTPUT].setVoltage(eonPulse.process(args.sampleTime)?10.f:0.f);
        outputs[CNTR_OUTPUT].setVoltage(slopeValue*10.f);
        lights[CYCLE_LIGHT].setBrightness(params[CYCLE_PARAM].getValue() > 0.5f ? slopeValue : 0.f);
        lights[TIMBRE_LIGHT].setBrightness(params[BALNC_PARAM].getValue());
        lights[ONSET_LIGHT].setBrightness(onsetPulse.process(args.sampleTime)?1.f:0.f);

        float mltCV=inputs[MLTPL_INPUT].isConnected()?inputs[MLTPL_INPUT].getVoltage()/10.f:slopeValue;
        float mltAmt=clamp(params[MULTIPLY_PARAM].getValue()+mltCV,0.f,1.f);
        float folded=waveFolder(shaped,mltAmt);

        // CONTOUR
        // ONSET = attack tijd
        // SUSTAIN = niveau tijdens gate
        // DECAY = decay/release tijd
        // EXP = curve shaping
        float sustain=params[SUSTAIN_PARAM].getValue();
        float onsetT=params[ONSET_PARAM].getValue(); // attack tijd (0=snel, 1=langzaam)
        float decT=params[DECAY_PARAM].getValue()+(inputs[DCY_INPUT].isConnected()?clamp(inputs[DCY_INPUT].getVoltage()/5.f,-2.f,4.f):0.f);
        float expAmt=params[EXP_PARAM].getValue();

        // Gate/trigger bepaalt contour fase
        bool contourGate=(slopeStage!=IDLE)||inputs[CNTR_INPUT].isConnected();
        float contourGateVal=inputs[CNTR_INPUT].isConnected()?inputs[CNTR_INPUT].getVoltage()/10.f:(contourGate?1.f:0.f);

        // Target: gate open = sustain niveau, gate dicht = 0
        float contourTarget=contourGateVal>0.01f?sustain:0.f;

        // Attack rate (ONSET knop: 0=instant, 1=langzaam)
        float attackT=onsetT*2.f; // 0 tot 2 seconden attack
        float rateUp=clamp(args.sampleTime/std::max(attackT,0.001f),0.f,1.f);

        // Decay rate met EXP curve
        if (expAmt != lastExpAmt) {
            cachedExpFactor = std::pow(10.f, expAmt * 2.f);
            lastExpAmt = expAmt;
        }
        float expFactor = cachedExpFactor;
        float rateDown=clamp(args.sampleTime/std::max(decT,0.001f)*expFactor,0.f,1.f);

        float contourRate=(contourTarget>contourValue)?rateUp:rateDown;
        contourValue+=(contourTarget-contourValue)*contourRate;
        outputs[CONTOUR_OUTPUT].setVoltage(contourValue*10.f);

        // DYN input neemt over, anders gebruikt Contour waarde
        float dynCV=inputs[DYNMC_INPUT].isConnected()?clamp(inputs[DYNMC_INPUT].getVoltage()/10.f,0.f,1.f):contourValue;
        dynCV=clamp((dynCV-0.01f)/0.99f,0.f,1.f);

        // BALANCE / TIMBRE section - V1.2
        // 1. Baseline sound - stabiele mix ~30% fold karakter
        float extIn=inputs[EXT_INPUT].isConnected()?inputs[EXT_INPUT].getVoltage()/10.f:0.f;
        float baseSound=tri*0.5f+folded*0.5f+extIn;

        // 2. Timbre laag - echte wavefolder vervorming zoals 0-Coast
        float sustainDrive=1.0f+clamp(sustain,0.f,1.f)*0.3f;
        float timbreTemp=clamp(params[BALANCE_PARAM].getValue(),0.f,1.f);
        if (inputs[TIMBRE_INPUT].isConnected()) timbreTemp+=inputs[TIMBRE_INPUT].getVoltage()/5.f;
        timbreTemp=clamp(timbreTemp,0.f,1.f);
        float shapedTimbreTemp=timbreTemp;
        float foldDrive=1.0f+shapedTimbreTemp*8.0f*sustainDrive;
        float driven=baseSound*foldDrive;
        float folded1=driven;
        folded1=std::fmod(folded1+1.f,4.f);
        if (folded1<0.f) folded1+=4.f;
        if (folded1>2.f) folded1=4.f-folded1;
        folded1=folded1-1.f;
        float folded2=driven*1.3f;
        folded2=std::fmod(folded2+1.f,4.f);
        if (folded2<0.f) folded2+=4.f;
        if (folded2>2.f) folded2=4.f-folded2;
        folded2=folded2-1.f;
        float timbreShaping=folded1*0.6f+folded2*0.3f+std::tanh(driven*0.5f)*0.1f;
        float extra=timbreShaping-baseSound;

        // 3. Timbre control - bereik 0.0 tot 1.0
        float timbreMax=clamp(params[BALANCE_PARAM].getValue(),0.f,1.f);
        float timbre=timbreMax;
        if (inputs[TIMBRE_INPUT].isConnected()) {
            float cvMod=inputs[TIMBRE_INPUT].getVoltage()/5.f;
            timbre=clamp(timbreMax*cvMod,0.f,timbreMax);
        }
        // Sustain beïnvloedt subtiel timbre en drive - hoog sustain = rijker karakter
        float sustainMod=clamp(sustain,0.f,1.f);
        timbre=clamp(timbre+sustainMod*0.2f,0.f,1.f);
        float shapedTimbre=timbre;

        // Sustain-gedreven dynamische drive - meer sustain = iets meer saturatie


        // 4. Timbre schakelaar
        bool timbreOn=params[BALNC_PARAM].getValue()>0.5f;

        // 5. Finale output - additief, baseline altijd intact
        float out=baseSound;
        if (timbreOn) {
            out=baseSound+(extra*shapedTimbre*0.5f);
        }

        // 6. Lichte gain compensatie, geen harde clipping
        out=std::tanh(out*0.9f)*1.1f;

        // Smooth alleen bij release
        float smoothCoeff = exp(-1.f / (0.002f * args.sampleRate));
        if (dynCV < smoothedDynCV)
            smoothedDynCV = smoothedDynCV * smoothCoeff + dynCV * (1.f - smoothCoeff);
        else
            smoothedDynCV = dynCV;
        outputs[LINEOUT_OUTPUT].setVoltage(out*smoothedDynCV*5.f);
    }
    json_t* dataToJson() override {
        json_t* rootJ=json_object();
        json_object_set_new(rootJ,"oversample2x",json_boolean(oversample2x));
        return rootJ;
    }
    void dataFromJson(json_t* rootJ) override {
        json_t* oversampleJ=json_object_get(rootJ,"oversample2x");
        if (oversampleJ) oversample2x=json_is_true(oversampleJ);
    }
};


template <const char* AssetPath>
struct DriftSubmitKnob : SvgKnob {
    DriftSubmitKnob() {
        minAngle=-0.83*M_PI;
        maxAngle=0.83*M_PI;
        setSvg(Svg::load(asset::plugin(pluginInstance,AssetPath)));
        shadow->opacity=0.f;
    }
};

static constexpr char DRIFT_KNOB_LARGE_ASSET[]="res/SubmitKnobLarge.png";
static constexpr char DRIFT_KNOB_MEDIUM_ASSET[]="res/SubmitKnobMedium.png";
static constexpr char DRIFT_KNOB_SMALL_ASSET[]="res/SubmitKnobSmall.png";

using DriftSubmitKnobLarge=DriftSubmitKnob<DRIFT_KNOB_LARGE_ASSET>;
using DriftSubmitKnobMedium=DriftSubmitKnob<DRIFT_KNOB_MEDIUM_ASSET>;
using DriftSubmitKnobSmall=DriftSubmitKnob<DRIFT_KNOB_SMALL_ASSET>;

struct Drift13Widget : ModuleWidget {
    Drift13Widget(Drift13* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance,"res/Drift13.png")));

        // OSCILLATOR
        addParam(createParamCentered<DriftSubmitKnobLarge>(mm2px(Vec(20.470f, 43.918f)), module, Drift13::PITCH_PARAM));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(28.910f, 76.041f)), module, Drift13::FINE_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.514f, 85.264f)), module, Drift13::VOCT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10.432f, 116.393f)), module, Drift13::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(23.773f, 116.427f)), module, Drift13::OUT2_OUTPUT));

        // OVERTONE + MULTIPLY
        addParam(createParamCentered<DriftSubmitKnobMedium>(mm2px(Vec(55.748f, 43.676f)), module, Drift13::OVERTONE_PARAM));
        addParam(createParamCentered<DriftSubmitKnobMedium>(mm2px(Vec(55.911f, 72.868f)), module, Drift13::MULTIPLY_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(49.226f, 116.425f)), module, Drift13::OVRTN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(62.464f, 116.415f)), module, Drift13::LINFM_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(62.492f, 100.699f)), module, Drift13::MLTPL_INPUT));

        // SLOPE
        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(90.86f, 29.25f)), module, Drift13::CYCLE_LIGHT));
        addParam(createParamCentered<CKSS>(mm2px(Vec(78.96f, 36.15f)), module, Drift13::CYCLE_PARAM));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(96.565f, 38.526f)), module, Drift13::RISE_PARAM));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(96.561f, 57.818f)), module, Drift13::FALL_PARAM));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(96.551f, 76.753f)), module, Drift13::TIME_PARAM));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(96.540f, 95.903f)), module, Drift13::LOGEXP_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(79.310f, 53.996f)), module, Drift13::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(79.428f, 116.448f)), module, Drift13::GATE_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(96.594f, 116.420f)), module, Drift13::SLOPE_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(79.295f, 69.578f)), module, Drift13::EON_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(79.297f, 85.059f)), module, Drift13::CNTR_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(79.303f, 100.774f)), module, Drift13::EOC_OUTPUT));

        // CONTOUR
        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(127.15f, 29.08f)), module, Drift13::ONSET_LIGHT));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(134.094f, 38.460f)), module, Drift13::ONSET_PARAM));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(134.094f, 57.733f)), module, Drift13::SUSTAIN_PARAM));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(134.094f, 76.680f)), module, Drift13::DECAY_PARAM));
        addParam(createParamCentered<DriftSubmitKnobSmall>(mm2px(Vec(134.094f, 95.945f)), module, Drift13::EXP_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(116.884f, 85.579f)), module, Drift13::DCY_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(116.866f, 100.726f)), module, Drift13::CNTR_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(116.787f, 116.441f)), module, Drift13::DYNMC_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(134.036f, 116.452f)), module, Drift13::CONTOUR_OUTPUT));

        // BALANCE / TIMBRE
        addParam(createParamCentered<DriftSubmitKnobMedium>(mm2px(Vec(162.094f, 43.762f)), module, Drift13::BALANCE_PARAM));
        addParam(createParamCentered<CKSS>(mm2px(Vec(153.18f, 67.63f)), module, Drift13::BALNC_PARAM));
        addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(153.15f, 60.62f)), module, Drift13::TIMBRE_LIGHT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(153.897f, 116.440f)), module, Drift13::TIMBRE_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(166.938f, 116.440f)), module, Drift13::LINEOUT_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        Drift13* module=dynamic_cast<Drift13*>(this->module);
        if (!module) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createCheckMenuItem("2x Oversampling", "",
            [=]() {return module->oversample2x;},
            [=]() {module->oversample2x=!module->oversample2x;}
        ));
    }
};

Model* modelDrift = createModel<Drift13, Drift13Widget>("Drift");
