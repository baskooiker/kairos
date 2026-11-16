/**
 * @file pattern.cpp
 * @brief Kompass style trigger generators and Turing machine CV sequences
 */

#include "kairos.h"
#include <stdlib.h>

// How far a Kompass weight may drift away from its character value, and how
// hard the drift is pushed on each pattern cycle
static const float DRIFT_RANGE = 20.0f;
static const float DRIFT_STEP  = 7.0f;
static const int   DRIFT_STEPS_PER_CYCLE = 3;

// How far the density control may throw a step away from its character value.
// Kept just under half the gap between the leading step and the weakest one, so
// a channel always keeps its strongest step no matter how often the density is
// swept, while steps in neighbouring tiers trade places freely.
static const float RANK_SHUFFLE_JITTER = 12.0f;

// The density has to move this far past the halfway point, in steps, before the
// count changes. Without it, control noise on a boundary would reshuffle the
// channel continuously.
static const float DENSITY_HYSTERESIS = 0.2f;

// The three loop lengths a Turing machine can settle on
static const uint8_t TURING_LENGTHS[3] = {8, 16, 32};

// How eagerly each machine follows the CTRL4 gesture. Machine 2 is a little
// less sensitive, so the same gesture rewrites it less and the pair stays
// distinct.
static const float TURING_SCALE[NUM_TURING_MACHINES] = {1.0f, 0.75f};

// Chance per step that a step is rewritten, at full gesture activity. Only one
// step is offered per clock step, so this has to be high for a knob move to
// audibly change the sequence right away.
static const float MAX_INJECTION = 0.9f;

// Chance per frame that a length morph starts, at full gesture activity
static const float MAX_LENGTH_CHANCE = 0.35f;

// Spread around the knob position of the values that get written, so a sweep
// paints a band of voltages rather than one flat level
static const float INJECT_SPREAD = 0.12f;

// ============================================================================
// HELPERS
// ============================================================================

/** Metric strength of a step in the 32 step grid (5 = bar, 1 = 16th offbeat) */
static int MetricStrength(int step)
{
    if(step % 16 == 0)
        return 5;
    if(step % 8 == 0)
        return 4;
    if(step % 4 == 0)
        return 3;
    if(step % 2 == 0)
        return 2;
    return 1;
}

/** Random float in 0..1 */
static float RandomFloat()
{
    return (float)rand() / (float)RAND_MAX;
}

/** Random float in -range..range */
static float RandomBipolar(float range)
{
    return RandomFloat() * 2.0f * range - range;
}

// ============================================================================
// KOMPASS CHANNEL
// ============================================================================

void KompassChannel::Init(KompassCharacter character)
{
    density     = 0.0f;
    activeSteps = 0;
    rotation    = 0;

    for(int s = 0; s < PATTERN_STEPS; s++)
    {
        int   metric = MetricStrength(s);
        float w      = 0.0f;

        switch(character)
        {
            case KOMPASS_DOWNBEAT:
                // Strong metric positions first, downbeat of bar 1 always leads
                w = (float)(metric * 10);
                if(s == 0)
                    w += 25.0f;
                break;

            case KOMPASS_BACKBEAT:
                // Beats 2 and 4 first, downbeats deliberately late
                if(s % 16 == 4 || s % 16 == 12)
                    w = 55.0f;
                else if(s % 16 == 0)
                    w = 8.0f;
                else
                    w = (float)(metric * 6);
                break;

            case KOMPASS_OFFBEAT:
                // 8th offbeats first, then 8ths, then the 16th subdivisions
                if(s % 4 == 2)
                    w = 50.0f;
                else if(s % 4 == 0)
                    w = 30.0f;
                else
                    w = 12.0f + (float)metric;
                break;
        }

        baseWeight[s] = w;
        shuffle[s]    = 0.0f;

        // Start the drift somewhere random so no two boots play the same thing
        drift[s] = RandomBipolar(DRIFT_RANGE * 0.5f);
    }

    ApplyDrift();
    ComputeRanks();
}

void KompassChannel::ApplyDrift()
{
    for(int s = 0; s < PATTERN_STEPS; s++)
        weight[s] = baseWeight[s] + drift[s] + shuffle[s];
}

/**
 * @brief Rank every step by weight, highest first
 *
 * The steps holding the first `keep` ranks are ranked among themselves and keep
 * those ranks; only the rest compete for what is left. That is what lets the
 * density control re-roll the pattern without ever dropping a step that is
 * currently firing. With `keep` at 0 the whole ranking is free to move, which
 * is what the slow automatic drift wants.
 */
void KompassChannel::ComputeRanks(int keep)
{
    bool taken[PATTERN_STEPS];
    bool locked[PATTERN_STEPS];

    for(int s = 0; s < PATTERN_STEPS; s++)
    {
        taken[s]  = false;
        locked[s] = rank[s] < keep;
    }

    for(int r = 0; r < PATTERN_STEPS; r++)
    {
        // The locked steps fill the first ranks, the free ones everything after
        bool wantLocked = r < keep;

        int   best      = -1;
        float bestValue = -1.0e9f;

        for(int s = 0; s < PATTERN_STEPS; s++)
        {
            if(taken[s] || locked[s] != wantLocked)
                continue;
            if(weight[s] > bestValue)
            {
                bestValue = weight[s];
                best      = s;
            }
        }

        // Cannot happen: exactly `keep` steps are locked, so each pass always
        // has a candidate. Guarded anyway rather than indexing with -1.
        if(best < 0)
            continue;

        taken[best] = true;
        rank[best]  = (uint8_t)r;
    }
}

void KompassChannel::Morph()
{
    // Random-walk a handful of steps so the ranking keeps rearranging itself.
    // The drift is clamped, which keeps the channel in character no matter how
    // long it runs.
    // Unlike a density move this is free to rearrange the whole ranking, hits
    // included. That is the slow evolution, and it is meant to be slow.
    for(int i = 0; i < DRIFT_STEPS_PER_CYCLE; i++)
    {
        int step = rand() % PATTERN_STEPS;

        drift[step] += RandomBipolar(DRIFT_STEP);

        if(drift[step] < -DRIFT_RANGE)
            drift[step] = -DRIFT_RANGE;
        if(drift[step] > DRIFT_RANGE)
            drift[step] = DRIFT_RANGE;
    }

    ApplyDrift();
    ComputeRanks();
}

/**
 * @brief Rotate the whole pattern in time
 * @param by Steps to move it forwards, negative moves it back
 *
 * Only the mapping from time to ranking moves, so a shift costs nothing and
 * survives the drift, the density control and everything else that rebuilds the
 * ranking.
 */
void KompassChannel::Shift(int by)
{
    rotation = (rotation + by) % PATTERN_STEPS;
    if(rotation < 0)
        rotation += PATTERN_STEPS;
}

/**
 * @brief Re-roll the steps that are not firing and rank them again
 * @param keep Number of leading ranks to leave untouched
 *
 * Every step outside the kept ranks gets a fresh random offset, so the order in
 * which the inactive steps are queued up changes completely while the hits that
 * are already playing stay exactly where they are.
 */
void KompassChannel::Reshuffle(int keep)
{
    for(int s = 0; s < PATTERN_STEPS; s++)
        if(rank[s] >= keep)
            shuffle[s] = RandomBipolar(RANK_SHUFFLE_JITTER);

    ApplyDrift();
    ComputeRanks(keep);
}

/**
 * @brief Apply a new density control value
 *
 * Any change to the step count, however small, re-rolls the steps that are not
 * firing. Going up therefore adds a hit that was not queued up before, and going
 * down and back up returns a different pattern rather than the one just left.
 * The steps that stay active across the change keep their ranks, so the count is
 * the only thing the control ever takes away.
 */
void KompassChannel::SetDensity(float value)
{
    density = value;

    // Stretch the usable part of the control travel over the whole range, so
    // the ends are reachable even on a control that falls short of them
    float scaled = NormalizeKnob(value) * (float)PATTERN_STEPS;
    int   target = (int)activeSteps;

    // Hysteresis around the current count, so control noise cannot sit on a
    // boundary and reshuffle the channel on every pass
    if(scaled > (float)activeSteps + 0.5f + DENSITY_HYSTERESIS)
        target = (int)(scaled - DENSITY_HYSTERESIS + 0.5f);
    else if(scaled < (float)activeSteps - 0.5f - DENSITY_HYSTERESIS)
        target = (int)(scaled + DENSITY_HYSTERESIS + 0.5f);

    if(target < 0)
        target = 0;
    if(target > PATTERN_STEPS)
        target = PATTERN_STEPS;

    if(target == (int)activeSteps)
        return;

    // Hits surviving the change are the ones both counts have room for
    int keep = target < (int)activeSteps ? target : (int)activeSteps;

    activeSteps = (uint8_t)target;
    Reshuffle(keep);
}

// ============================================================================
// TURING MACHINE
// ============================================================================

/** Copy a step's value to the later steps that share its position in the loop */
void TuringMachine::PropagateForward(int step, int len)
{
    for(int q = step + len; q < PATTERN_STEPS; q += len)
        value[q] = value[step];
}

/** Copy a step's value to every step that shares its position in the loop */
void TuringMachine::ReplicateClass(int step, int len)
{
    for(int q = step % len; q < PATTERN_STEPS; q += len)
        value[q] = value[step];
}

void TuringMachine::Init(uint32_t seed, float scale)
{
    srand(seed);

    for(int s = 0; s < PATTERN_STEPS; s++)
        value[s] = (uint8_t)(rand() & 0xFF);

    length         = TURING_LENGTHS[rand() % 3];
    pendingLength  = 0;
    injectionScale = scale;
    target         = 0.0f;

    // Make the frame periodic with the starting length
    for(int s = 0; s < length; s++)
        PropagateForward(s, length);
}

/**
 * @brief Commit a finished length morph, or roll for a new one
 *
 * Called at step 0 so a length morph always spans exactly one frame. Like the
 * step values, the length only ever changes while CTRL4 is being moved.
 */
void TuringMachine::BeginFrame()
{
    if(pendingLength)
    {
        length        = pendingLength;
        pendingLength = 0;
        return;
    }

    float chance = knobActivity * injectionScale * MAX_LENGTH_CHANCE;

    if(RandomFloat() >= chance)
        return;

    uint8_t candidate = TURING_LENGTHS[rand() % 3];
    if(candidate != length)
        pendingLength = candidate;
}

float TuringMachine::Step(int step)
{
    int activeLength = pendingLength ? pendingLength : length;

    // Write a new value into this step if the CTRL4 gesture calls for it. The
    // whole loop position is rewritten, so every repetition of it in the frame
    // follows along.
    float chance = knobActivity * injectionScale * MAX_INJECTION;

    if(RandomFloat() < chance)
    {
        float painted = target + RandomBipolar(INJECT_SPREAD);

        if(painted < 0.0f)
            painted = 0.0f;
        if(painted > 1.0f)
            painted = 1.0f;

        value[step] = (uint8_t)(painted * 255.0f);
        ReplicateClass(step, activeLength);
    }

    // A length morph is applied one step at a time as the playhead sweeps the
    // frame. Shrinking pulls the later repetitions onto this step; growing
    // propagates nothing, so the repetitions simply drift apart from here on.
    if(pendingLength)
        PropagateForward(step, pendingLength);

    return (float)value[step] / 255.0f;
}

// ============================================================================
// PATTERN PROCESSING
// ============================================================================

void InitPatterns()
{
    uint32_t seed = System::GetUs();

    srand(seed);

    kompass[0].Init(KOMPASS_DOWNBEAT);
    kompass[1].Init(KOMPASS_BACKBEAT);
    kompass[2].Init(KOMPASS_OFFBEAT);

    for(int t = 0; t < NUM_TURING_MACHINES; t++)
        turing[t].Init(seed + 15485863u * (uint32_t)(t + 1), TURING_SCALE[t]);

    srand(seed);
}

/**
 * @brief Fire the reset pulse on the gate output
 *
 * Called just before the transport starts, or ahead of the downbeat of a
 * resync, so downstream sequencers are already rewound when the clock pulse
 * reaches them. The gate is raised here rather than on the next main loop pass,
 * so it always leads the clock pulse the audio callback is about to output.
 */
void FireReset()
{
    resetOut        = true;
    resetOutCounter = 0;
    dsy_gpio_write(&hw.gate_output, 1);
}

void ProcessStep(int step)
{
    // OUT4: 16th note clock, fires on every step
    clockOut        = true;
    clockOutCounter = 0;

    if(step == 0)
    {
        // Slow evolution of the trigger patterns and the loop lengths
        for(int c = 0; c < NUM_KOMPASS_CHANNELS; c++)
            kompass[c].Morph();

        for(int t = 0; t < NUM_TURING_MACHINES; t++)
            turing[t].BeginFrame();
    }

    // OUT1-3: Kompass density channels
    bool triggered[NUM_KOMPASS_CHANNELS];

    for(int c = 0; c < NUM_KOMPASS_CHANNELS; c++)
    {
        triggered[c] = kompass[c].StepActive(step);

        if(triggered[c])
        {
            triggerOut[c]     = true;
            triggerCounter[c] = 0;
        }
    }

    // CV1-2: the Turing machines always advance so they stay locked to the
    // frame, but the voltage is only sampled when the Kompass channel they are
    // paired with actually fires
    for(int t = 0; t < NUM_TURING_MACHINES; t++)
    {
        float value = turing[t].Step(step);

        if(triggered[t])
            cvOut[t] = value;
    }
}

void WriteCvOutputs()
{
    for(int t = 0; t < NUM_TURING_MACHINES; t++)
    {
        uint16_t value = (uint16_t)(cvOut[t] * 4095.0f);
        if(value > 4095)
            value = 4095;

        hw.seed.dac.WriteValue(t == 0 ? DacHandle::Channel::ONE
                                      : DacHandle::Channel::TWO,
                               value);
    }
}
