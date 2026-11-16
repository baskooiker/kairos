/**
 * @file kairos.h
 * @brief Shared types, constants and global state for Kairos
 */

#ifndef KAIROS_H
#define KAIROS_H

#include "daisy_patch.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

// ============================================================================
// CONSTANTS
// ============================================================================

static const int PATTERN_STEPS   = 32;  // 2 bars of 16th notes
static const int PPQN            = 24;  // MIDI clock pulses per quarter note
static const int PULSES_PER_STEP = PPQN / 4;
static const int STEPS_PER_BAR   = 16; // a resync lands on the next bar line

static const int NUM_KOMPASS_CHANNELS = 2;
static const int NUM_TURING_MACHINES  = 2;

// Gate outputs on the audio jacks: OUT1-3. OUT4 is the 16th note clock.
enum GateOutput
{
    GATE_A,        // OUT1, channel A gate
    GATE_A_ACCENT, // OUT2, channel A accent
    GATE_B,        // OUT3, channel B gate
    NUM_GATE_OUTS,
};

// Gate lengths in clock pulses (PULSES_PER_STEP to a step), so they follow the
// tempo under every clock source
static const int SHORT_GATE_PULSES   = 2; // a third of a step
static const int REGULAR_GATE_PULSES = 3; // half a step

// A tie holds the gate through at most this many following empty steps, and
// lets it go halfway through the last one so the next hit still retriggers
static const int MAX_TIE_STEPS = 3;

// Every gate rises this long after its step: it guarantees a low gap before a
// gate that is still high is raised again, and lets the CV settle first
static const uint32_t GATE_DELAY_SAMPLES = 48; // 1ms

static const float MIN_BPM     = 20.0f;
static const float MAX_BPM     = 300.0f;
static const float DEFAULT_BPM = 120.0f;

// Clock and reset pulse widths in samples (48kHz)
static const uint32_t TRIGGER_PULSE_SAMPLES = 480; // 10ms
static const uint32_t RESET_PULSE_SAMPLES   = 960; // 20ms

// External clock is considered lost after this many milliseconds without a pulse
static const uint32_t EXTERNAL_CLOCK_TIMEOUT_MS = 500;

// GATE IN 1 is an analog clock input: one pulse is one pattern step, so four
// pulses make a quarter note
static const int GATE_CLOCK_STEPS_PER_BEAT = 4;

// Pulse intervals outside the supported tempo range are treated as glitches
static const uint32_t MIN_GATE_INTERVAL_MS = 50;  // 300 BPM
static const uint32_t MAX_GATE_INTERVAL_MS = 750; // 20 BPM

// The analog clock is considered lost after this long without a pulse. It has
// to outlast one step at the slowest tempo, which is 750ms.
static const uint32_t GATE_CLOCK_TIMEOUT_MS = 1500;

// MIDI system realtime bytes
static const uint8_t MIDI_CLOCK    = 0xF8;
static const uint8_t MIDI_START    = 0xFA;
static const uint8_t MIDI_CONTINUE = 0xFB;
static const uint8_t MIDI_STOP     = 0xFC;
static const uint8_t MIDI_RESET    = 0xFF;

// MIDI system common
static const uint8_t MIDI_SONG_POSITION = 0xF2;

// Fraction of the travel folded into each end of a control's range. The panel
// controls do not all reach their electrical ends, so without this the extremes
// of a range need the outer 1% of the travel, which CTRL1 in particular does not
// manage. It costs a little resolution in the middle and makes both ends
// reliable on every control.
static const float KNOB_MARGIN = 0.05f;

/** @brief Stretch the usable part of a control's travel over the full 0..1 */
static inline float NormalizeKnob(float value)
{
    float normalised = (value - KNOB_MARGIN) / (1.0f - 2.0f * KNOB_MARGIN);

    if(normalised < 0.0f)
        normalised = 0.0f;
    if(normalised > 1.0f)
        normalised = 1.0f;

    return normalised;
}

// ============================================================================
// TYPES
// ============================================================================

/** What the encoder is pointing at */
enum MenuItem
{
    MENU_TEMPO,
    MENU_TRANSPORT,
    MENU_RESYNC,
    MENU_PATTERN_1,
    MENU_PATTERN_2,
    MENU_LAST,
};

/** Rhythmic character of a Kompass channel */
enum KompassCharacter
{
    KOMPASS_DOWNBEAT, // kick-like: strong metric positions first
    KOMPASS_BACKBEAT, // snare-like: beats 2 and 4 first
};

/** How long a hit holds its gate */
enum GateShape
{
    GATE_SHAPE_SHORT,   // a third of a step
    GATE_SHAPE_REGULAR, // half a step
    GATE_SHAPE_TIE,     // through the empty steps that follow, if any
};

/**
 * @brief Bastl Kompass style density-driven trigger channel
 *
 * Every step of the pattern gets a priority rank derived from a weight table.
 * The density control decides how many of the 32 steps are allowed to fire;
 * steps with the lowest rank fire first, so turning density up adds triggers
 * without rearranging the ones that are already playing.
 *
 * The weights are a fixed part per step that carries the rhythmic character of
 * the channel, a slowly drifting part, and a part that is re-rolled whenever the
 * density control moves. The drift random-walks a few steps per pattern cycle,
 * so the channel grows new patterns on its own over many minutes; the density
 * control gives the same thing on demand.
 *
 * The whole pattern can be rotated in time without touching the ranking, so a
 * shift survives both the drift and the density control.
 *
 * Moving the density re-rolls and re-ranks only the steps that are not firing,
 * so the steps currently in the pattern keep their ranks. Turning density up
 * therefore only ever adds hits, but which hits get added is new every time: a
 * sweep down and back up comes back with a different pattern in the same
 * character.
 */
struct KompassChannel
{
    float   baseWeight[PATTERN_STEPS];
    float   drift[PATTERN_STEPS];   // slow automatic evolution
    float   shuffle[PATTERN_STEPS]; // re-rolled by the density control
    float   weight[PATTERN_STEPS];  // sum of the three, what the ranks use
    uint8_t rank[PATTERN_STEPS];
    uint8_t shape[PATTERN_STEPS];    // GateShape of the hit, if it fires
    uint8_t tieSteps[PATTERN_STEPS]; // empty steps a tie may hold through
    float   density;     // raw control value, for reference
    uint8_t activeSteps; // steps allowed to fire, hysteresis applied
    int     rotation;    // steps the pattern is shifted forwards in time

    void Init(KompassCharacter character);
    void ApplyDrift();
    void ComputeRanks(int keep = 0);
    void Morph();
    void SetDensity(float value);
    void Reshuffle(int keep);
    void Shift(int by);
    void RollShape(int s);

    /** How many of the 32 steps are currently firing */
    int ActiveSteps() const { return (int)activeSteps; }

    /** Ranking index of a position in time */
    int Unrotate(int step) const
    {
        // The rotation maps a position in time back onto the ranking, so the
        // ranking itself never has to move
        int s = step - rotation;
        if(s < 0)
            s += PATTERN_STEPS;
        return s;
    }

    bool StepActive(int step) const { return rank[Unrotate(step)] < activeSteps; }

    /** Accents go on the strongest hits, roughly a quarter of them */
    bool StepAccented(int step) const
    {
        return rank[Unrotate(step)] < (activeSteps + 3) / 4;
    }

    int TiedSteps(int step) const;
    int GatePulses(int step) const;
};

/**
 * @brief Turing machine style sequence living in a 32 step frame
 *
 * The sequence always spans the 32 step pattern, but its loop length can be 8,
 * 16 or 32: at length 8 the frame holds four repetitions of the same 8 steps.
 * A length change is not a switch, it is applied one step at a time over a
 * full frame, so the repetitions collapse into each other (or grow apart) as
 * the playhead sweeps past them.
 *
 * The loop never changes on its own. New values are written into it only while
 * its knob (CTRL2 for machine 1, CTRL4 for machine 2) is being moved: the movement sets how likely a step is to be rewritten
 * and the knob position sets what gets written, so a sweep through low values
 * paints low voltages into the sequence and a sweep up high paints high ones.
 * Stop moving and the loop locks again.
 */
struct TuringMachine
{
    uint8_t value[PATTERN_STEPS]; // one 8-bit value per step of the frame
    uint8_t length;               // 8, 16 or 32; the frame is periodic with it
    uint8_t pendingLength;        // length being morphed towards, 0 if settled
    float   activity;             // 0..1, gesture level of this machine's knob
    float   target;               // 0..1, value the gesture is writing now

    void  Init(uint32_t seed);
    void  BeginFrame();   // call at step 0: commits or starts a length morph
    float Step(int step); // advances to `step`, returns CV in 0..1

    void PropagateForward(int step, int len);
    void ReplicateClass(int step, int len);
};

// ============================================================================
// GLOBAL STATE
// ============================================================================

extern DaisyPatch hw;

// Clock
extern float    tempoBpm;
extern bool     isRunning;
extern bool     externalClockMode;
extern uint32_t lastExternalPulseMs;
extern bool     externalStopped; // external master sent Stop, ignore its clock
extern int      pulseInStep;   // 0..PULSES_PER_STEP-1
extern int      currentStep;   // 0..PATTERN_STEPS-1
extern bool     resyncArmed;   // reset downstream gear on the next bar line

// Analog clock patched into GATE IN 1
extern bool     gateClockMode;
extern uint32_t lastGatePulseMs;
extern bool     gateAwaitingFirstStep; // next edge plays currentStep as-is
extern int      pulsesSinceGateEdge;   // MIDI clocks sent for the current step

// Internal clock pulse generation (audio callback -> main loop)
extern volatile uint32_t internalPulsesGenerated;
extern uint32_t          internalPulsesConsumed;
extern float             samplesPerPulse;
extern volatile bool     pulsePhaseReset; // realign the generator to "now"

// Gate outputs. The main loop raises a gate and counts its length down in
// clock pulses; the audio callback holds each rising edge back by the gate delay.
extern volatile bool     gateOut[NUM_GATE_OUTS];
extern volatile uint32_t gateDelayCounter[NUM_GATE_OUTS];
extern int               gatePulsesLeft[NUM_GATE_OUTS];
extern volatile bool     clockOut;
extern volatile uint32_t clockOutCounter;
extern volatile bool     resetOut;
extern volatile uint32_t resetOutCounter;

// Encoder menu
extern int  menuItem;   // MenuItem the encoder is pointing at
extern bool menuActive; // true while the item is being edited

// Pattern generators
extern KompassChannel kompass[NUM_KOMPASS_CHANNELS];
extern TuringMachine  turing[NUM_TURING_MACHINES];
extern float          cvOut[NUM_TURING_MACHINES];

// ============================================================================
// FUNCTIONS
// ============================================================================

// clock.cpp
void UpdateSamplesPerPulse();
void StartClock(bool fromContinue); // state only, caller sends MIDI transport
void StopClock();                   // state only, caller sends MIDI transport
void SendMidiRealtime(uint8_t byte);
void SendMidiSongPosition(uint16_t beats);
void ResetClock();
void OnClockPulse(bool forwardToMidiOut);
void ProcessControls();
void HandleMidiMessage(MidiEvent m);
void CheckExternalClockTimeout();
void ProcessGateInputs();
void OnGateClockEdge();
void OnGateResetEdge();
void FillGateClockPulse();
void CheckGateClockTimeout();
void ApplyPendingResync();

// pattern.cpp
void InitPatterns();
void ProcessStep(int step);
void TickGates();  // once per clock pulse, before ProcessStep
void ClearGates();
void FireReset();
void WriteCvOutputs();

// display.cpp
void UpdateDisplay();

// storage.cpp
void LoadSettings();        // call once after hw.Init(), restores the tempo
void OnTempoEdited();       // the encoder changed the tempo, save it later
void ProcessSettingsSave(); // writes a pending save once it is safe to

#endif // KAIROS_H
