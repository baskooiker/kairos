/**
 * @file clock.cpp
 * @brief Clock generation, MIDI clock in/out and control handling
 */

#include "kairos.h"

// ============================================================================
// MIDI OUTPUT
// ============================================================================

void SendMidiRealtime(uint8_t byte)
{
    hw.midi.SendMessage(&byte, 1);
}

/** @brief Tell MIDI out to relocate; `beats` counts 16th notes from the start */
void SendMidiSongPosition(uint16_t beats)
{
    uint8_t message[3] = {MIDI_SONG_POSITION,
                          (uint8_t)(beats & 0x7F),
                          (uint8_t)((beats >> 7) & 0x7F)};
    hw.midi.SendMessage(message, 3);
}

// ============================================================================
// CLOCK STATE
// ============================================================================

void UpdateSamplesPerPulse()
{
    float samplerate = hw.AudioSampleRate();
    samplesPerPulse  = (samplerate * 60.0f) / (tempoBpm * (float)PPQN);
}

void StartClock(bool fromContinue)
{
    if(!fromContinue)
    {
        pulseInStep = 0;
        currentStep = 0;

        // Reset goes out ahead of the first clock pulse, not with it
        FireReset();

        // Everything downstream is rewound anyway
        resyncArmed = false;
    }
    isRunning = true;
}

void StopClock()
{
    isRunning   = false;
    resyncArmed = false;

    // Leave the pulse phase alone so Continue can pick up where we stopped
    ClearGates();
    clockOut = false;
}

void ResetClock()
{
    pulseInStep = 0;
    currentStep = 0;
}

/**
 * @brief Fire an armed resync if the step about to play starts a bar
 *
 * Must be called at a step boundary, before the clock byte of that step goes to
 * MIDI out. Downstream gear is rewound without stopping Kairos: the reset pulse
 * goes out on GATE OUT and MIDI out gets a Stop and a Start, so the clock byte
 * that follows plays the first step on every receiver. Stop is sent ahead of
 * Start because not every receiver restarts on a Start while it is running.
 * Kairos itself keeps its position in the pattern.
 */
void ApplyPendingResync()
{
    if(!resyncArmed || currentStep % STEPS_PER_BAR != 0)
        return;

    resyncArmed = false;

    FireReset();
    SendMidiRealtime(MIDI_STOP);
    SendMidiRealtime(MIDI_START);
}

/**
 * @brief Advance the clock by one MIDI pulse (1/24th note)
 * @param forwardToMidiOut Send a clock byte to MIDI out for this pulse
 */
void OnClockPulse(bool forwardToMidiOut)
{
    // A resync has to reach MIDI out ahead of the clock byte of its downbeat
    if(isRunning && pulseInStep == 0)
        ApplyPendingResync();

    if(forwardToMidiOut)
        SendMidiRealtime(MIDI_CLOCK);

    if(!isRunning)
        return;

    // Gate lengths count clock pulses; the tick comes first so a gate ending on
    // a step boundary is down before the step raises the next one
    TickGates();

    if(pulseInStep == 0)
        ProcessStep(currentStep);

    pulseInStep++;
    if(pulseInStep >= PULSES_PER_STEP)
    {
        pulseInStep = 0;
        currentStep = (currentStep + 1) % PATTERN_STEPS;
    }
}

/**
 * @brief Fall back to the internal clock when the external master goes away
 */
void CheckExternalClockTimeout()
{
    if(!externalClockMode)
        return;

    uint32_t now = System::GetNow();
    if(now - lastExternalPulseMs < EXTERNAL_CLOCK_TIMEOUT_MS)
        return;

    externalClockMode = false;
    externalStopped   = false;

    if(isRunning)
    {
        StopClock();
        SendMidiRealtime(MIDI_STOP);
    }

    // Resync the internal pulse generator to "now"
    internalPulsesConsumed = internalPulsesGenerated;
}

// ============================================================================
// ANALOG CLOCK AND RESET INPUTS
// ============================================================================

/**
 * @brief Handle a rising edge on GATE IN 1, the analog clock input
 *
 * One pulse is one pattern step. The interval between two pulses sets the
 * tempo, which lets the internal 24 PPQN generator fill in the pulses between
 * the edges so MIDI out still carries a proper clock. Steps themselves are only
 * ever advanced by an edge, so the pattern cannot drift away from the master no
 * matter how far the derived tempo is off.
 */
void OnGateClockEdge()
{
    uint32_t now = System::GetNow();

    if(gateClockMode)
    {
        uint32_t interval = now - lastGatePulseMs;

        // Anything outside the tempo range is a glitch, not a tempo change
        if(interval >= MIN_GATE_INTERVAL_MS && interval <= MAX_GATE_INTERVAL_MS)
        {
            tempoBpm = 60000.0f
                       / ((float)interval * (float)GATE_CLOCK_STEPS_PER_BEAT);

            if(tempoBpm < MIN_BPM)
                tempoBpm = MIN_BPM;
            if(tempoBpm > MAX_BPM)
                tempoBpm = MAX_BPM;

            UpdateSamplesPerPulse();
        }
    }
    else
    {
        // A patched clock takes over from MIDI and from the internal clock
        externalClockMode     = false;
        externalStopped       = false;
        gateClockMode         = true;
        gateAwaitingFirstStep = true;
        ResetClock();
    }

    lastGatePulseMs = now;

    if(!isRunning)
    {
        StartClock(false);
        SendMidiRealtime(MIDI_START);
        gateAwaitingFirstStep = true;
    }

    // The first edge after a start or a reset plays the step we are already on
    if(gateAwaitingFirstStep)
        gateAwaitingFirstStep = false;
    else
        currentStep = (currentStep + 1) % PATTERN_STEPS;

    // The edge is the step boundary, so re-phase the pulse filler onto it
    pulseInStep            = 0;
    internalPulsesConsumed = internalPulsesGenerated;
    pulsePhaseReset        = true;

    ApplyPendingResync();

    SendMidiRealtime(MIDI_CLOCK);
    pulsesSinceGateEdge = 1;

    TickGates();
    ProcessStep(currentStep);
}

/**
 * @brief Handle a rising edge on GATE IN 2, the reset input
 *
 * Rewinds the pattern so the next clock plays step 0, echoes the reset on the
 * gate output and tells MIDI out to rewind as well.
 */
void OnGateResetEdge()
{
    ResetClock();
    gateAwaitingFirstStep = true;
    pulsesSinceGateEdge   = 0;
    resyncArmed           = false;

    FireReset();
    SendMidiSongPosition(0);
}

/**
 * @brief Emit one of the MIDI clock bytes that fall between two gate edges
 *
 * Only the pulses belonging to the current step are sent. If the derived tempo
 * runs slightly fast the generator simply waits for the next edge, so MIDI out
 * never gets more than 24 clocks per quarter note.
 */
void FillGateClockPulse()
{
    if(!isRunning)
        return;
    if(pulsesSinceGateEdge >= PULSES_PER_STEP)
        return;

    pulsesSinceGateEdge++;
    SendMidiRealtime(MIDI_CLOCK);
    TickGates();
}

/** @brief Fall back to the internal clock when the patched clock goes away */
void CheckGateClockTimeout()
{
    if(!gateClockMode)
        return;

    if(System::GetNow() - lastGatePulseMs < GATE_CLOCK_TIMEOUT_MS)
        return;

    gateClockMode = false;

    if(isRunning)
    {
        StopClock();
        SendMidiRealtime(MIDI_STOP);
    }

    // Resync the internal pulse generator to "now"
    internalPulsesConsumed = internalPulsesGenerated;
}

/**
 * @brief Poll both gate inputs; must be called once per main loop pass
 *
 * GateIn::Trig() detects the edge on the call itself, so each input has to be
 * read exactly once per pass or edges are missed.
 */
void ProcessGateInputs()
{
    bool clockEdge = hw.gate_input[DaisyPatch::GATE_IN_1].Trig();
    bool resetEdge = hw.gate_input[DaisyPatch::GATE_IN_2].Trig();

    // Reset first, so a reset landing on the same edge as a clock makes that
    // clock play step 0
    if(resetEdge)
        OnGateResetEdge();

    if(clockEdge)
        OnGateClockEdge();
}

// ============================================================================
// CONTROLS
// ============================================================================

// The write gestures on CTRL2 and CTRL4 are measured over a window instead of
// per control tick: at the 1kHz control rate a deliberate sweep moves the knob by less than the ADC
// noise floor within a single tick, so a per-tick deadband only ever let a
// flick of the wrist through and the write probability stayed near zero.
static const uint32_t KNOB_WINDOW_TICKS = 20; // 20ms measurement window

// Movement below this much per window is treated as noise, otherwise the
// gesture would never fade out
static const float KNOB_DEADBAND = 0.002f;

// Movement gain, the level that any deliberate movement buys immediately, and
// the per-tick decay of the gesture. ProcessControls runs at 1kHz, so 0.9985
// fades a gesture out over roughly a second.
static const float KNOB_ACTIVITY_GAIN  = 12.0f;
static const float KNOB_ACTIVITY_FLOOR = 0.4f;
static const float KNOB_ACTIVITY_DECAY = 0.9985f;

// Windows spent tracking the knob before the gesture starts counting
static const uint32_t KNOB_PRIME_WINDOWS = 5;

/** @brief Is this menu item reachable right now? */
static bool MenuItemAvailable(int item)
{
    // Under any external clock neither the tempo nor the transport is ours
    if(item == MENU_TEMPO || item == MENU_TRANSPORT)
        return !externalClockMode && !gateClockMode;

    // There is nothing to resync while the sequencer is stopped, whoever the
    // clock master is
    if(item == MENU_RESYNC)
        return isRunning;

    return true;
}

/** @brief Move the highlight, skipping whatever is unavailable */
static void SelectMenuItem(int increment)
{
    int direction = increment > 0 ? 1 : -1;
    int moves     = increment > 0 ? increment : -increment;

    for(int m = 0; m < moves; m++)
    {
        // The guard stops the search if nothing at all is available, which
        // cannot happen while the patterns are always reachable
        for(int guard = 0; guard < MENU_LAST; guard++)
        {
            menuItem = (menuItem + direction + MENU_LAST) % MENU_LAST;
            if(MenuItemAvailable(menuItem))
                break;
        }
    }
}

/** @brief Turn the encoder with an item activated */
static void EditMenuItem(int increment)
{
    if(menuItem == MENU_TEMPO)
    {
        tempoBpm += (float)increment;

        if(tempoBpm < MIN_BPM)
            tempoBpm = MIN_BPM;
        if(tempoBpm > MAX_BPM)
            tempoBpm = MAX_BPM;

        UpdateSamplesPerPulse();
        OnTempoEdited();
        return;
    }

    kompass[menuItem - MENU_PATTERN_1].Shift(increment);
}

/** @brief Start or stop the sequencer, only when we are the clock master */
static void ToggleTransport()
{
    if(externalClockMode || gateClockMode)
        return;

    if(isRunning)
    {
        StopClock();
        SendMidiRealtime(MIDI_STOP);
    }
    else
    {
        ResetClock();
        StartClock(false);
        SendMidiRealtime(MIDI_START);

        // First pulse of the new bar happens immediately
        internalPulsesConsumed = internalPulsesGenerated;
        OnClockPulse(true);
    }
}

/**
 * @brief Arm a resync for the next bar line, or cancel one already armed
 *
 * Works under any clock source, since the point is to realign the gear that
 * follows Kairos while the music keeps playing.
 */
static void ToggleResync()
{
    if(!isRunning)
        return;

    resyncArmed = !resyncArmed;
}

/**
 * @brief Encoder menu: turn to choose, press to edit, press again to leave
 *
 * The transport and the resync are the exceptions: there is nothing to turn
 * once they are chosen, so their press acts straight away instead of
 * activating anything.
 */
static void ProcessMenu()
{
    // An external clock takes the tempo and the transport away, and stopping
    // takes the resync away, while the menu may be sitting on one of them. Fall
    // back to the transport if it is still ours, otherwise to a pattern.
    if(!MenuItemAvailable(menuItem))
    {
        menuItem   = MenuItemAvailable(MENU_TRANSPORT) ? MENU_TRANSPORT
                                                       : MENU_PATTERN_1;
        menuActive = false;
    }

    if(hw.encoder.RisingEdge())
    {
        if(menuItem == MENU_TRANSPORT)
            ToggleTransport();
        else if(menuItem == MENU_RESYNC)
            ToggleResync();
        else
            menuActive = !menuActive;
    }

    int increment = hw.encoder.Increment();
    if(increment == 0)
        return;

    if(menuActive)
        EditMenuItem(increment);
    else
        SelectMenuItem(increment);
}

void ProcessControls()
{
    hw.ProcessAllControls();

    ProcessMenu();

    // CTRL1 and CTRL3: channel A and B densities. Moving one re-rolls the steps
    // that channel is not playing, so the hits it gains are new ones.
    kompass[0].SetDensity(hw.GetKnobValue(DaisyPatch::CTRL_1));
    kompass[1].SetDensity(hw.GetKnobValue(DaisyPatch::CTRL_3));

    // CTRL2 and CTRL4: write gestures for Turing machine 1 and 2
    static const DaisyPatch::Ctrl GESTURE_KNOB[NUM_TURING_MACHINES]
        = {DaisyPatch::CTRL_2, DaisyPatch::CTRL_4};

    static uint32_t knobWindowTicks  = 0;
    static uint32_t knobPrimeWindows = KNOB_PRIME_WINDOWS;
    static float    lastKnobValue[NUM_TURING_MACHINES] = {0.0f, 0.0f};

    bool windowDone = ++knobWindowTicks >= KNOB_WINDOW_TICKS;
    if(windowDone)
        knobWindowTicks = 0;

    // The ADC needs a moment to settle after boot; until it has, only the
    // reference is tracked, so a knob's resting position is not mistaken for a
    // gesture
    bool primed = knobPrimeWindows == 0;
    if(windowDone && !primed)
        knobPrimeWindows--;

    for(int t = 0; t < NUM_TURING_MACHINES; t++)
    {
        // Moving the knob raises the chance that steps get rewritten; the knob
        // position decides what gets written. The level fades once the knob
        // stops, which locks the loop. Movement is measured on the raw travel,
        // so a gesture still registers at either extreme; the value being
        // painted uses the stretched range, so the sequence can reach the ends
        // of the CV range on a control that falls short of its own.
        float knobRaw = hw.GetKnobValue(GESTURE_KNOB[t]);

        bool moved = false;

        if(windowDone)
        {
            float delta = knobRaw - lastKnobValue[t];
            if(delta < 0.0f)
                delta = -delta;

            moved = primed && delta > KNOB_DEADBAND;

            if(moved)
            {
                turing[t].activity += delta * KNOB_ACTIVITY_GAIN;

                // Any deliberate movement is worth a usable write chance
                // straight away rather than having to be built up first
                if(turing[t].activity < KNOB_ACTIVITY_FLOOR)
                    turing[t].activity = KNOB_ACTIVITY_FLOOR;
                if(turing[t].activity > 1.0f)
                    turing[t].activity = 1.0f;
            }

            lastKnobValue[t] = knobRaw;
        }

        turing[t].activity *= KNOB_ACTIVITY_DECAY;
        if(turing[t].activity < 0.0001f)
            turing[t].activity = 0.0f;

        // The knob position is the value being painted, whether or not the
        // gesture is currently active
        turing[t].target = NormalizeKnob(knobRaw);

        // On top of the gesture the knob plays the scale over MIDI. Until the
        // ADC has settled it only learns where the knob rests.
        if(primed)
            ProcessKnobNote(t, turing[t].target, moved, System::GetNow());
    }
}

// ============================================================================
// MIDI INPUT
// ============================================================================

void HandleMidiMessage(MidiEvent m)
{
    // A clock patched into GATE IN 1 outranks MIDI: while it is running the
    // incoming clock and transport are ignored rather than fought with
    if(gateClockMode && (m.type == SystemRealTime || m.type == SystemCommon))
        return;

    switch(m.type)
    {
        case SystemRealTime:
            switch(m.srt_type)
            {
                case TimingClock:
                    lastExternalPulseMs = System::GetNow();

                    if(!externalClockMode)
                    {
                        // Hand over from internal to external clock
                        externalClockMode = true;
                        externalStopped   = false;

                        if(isRunning)
                            pulseInStep = 0;
                        else
                            ResetClock();
                    }

                    // A master that only sends clock (no Start) still runs us.
                    // The handover above rewound us, so this start resets too.
                    if(!isRunning && !externalStopped)
                        StartClock(false);

                    OnClockPulse(true);
                    break;

                case Start:
                    SendMidiRealtime(MIDI_START);
                    externalClockMode = true;
                    externalStopped   = false;
                    ResetClock();
                    StartClock(false);
                    break;

                case Continue:
                    SendMidiRealtime(MIDI_CONTINUE);
                    externalClockMode = true;
                    externalStopped   = false;
                    StartClock(true);
                    break;

                case Stop:
                    SendMidiRealtime(MIDI_STOP);
                    externalStopped = true;
                    StopClock();
                    break;

                case Reset:
                    SendMidiRealtime(MIDI_RESET);
                    externalStopped = true;
                    StopClock();
                    ResetClock();
                    FireReset();
                    break;

                default: break;
            }
            break;

        case SystemCommon:
            // Song position pointer counts MIDI beats (16th notes)
            if(m.sc_type == SongPositionPointer)
            {
                int beats   = ((int)m.data[1] << 7) | (int)m.data[0];
                currentStep = beats % PATTERN_STEPS;
                pulseInStep = 0;
            }
            break;

        default: break;
    }
}
