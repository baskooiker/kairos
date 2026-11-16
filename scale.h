/**
 * @file scale.h
 * @brief Slowly morphing scale that the Turing machines are quantised to
 *
 * A scale is a set of pitch classes plus a root, which is always in the set.
 * It changes one note at a time in a slow cycle:
 *
 *   1. Shrink: notes are removed, the ones that give the scale a major or minor
 *      colour first (thirds, then sixths and sevenths), until only a modal
 *      skeleton of three notes without a third is left.
 *   2. Shift: the root moves to one of the notes still in the skeleton,
 *      preferably its fourth or fifth, so the set itself does not change.
 *   3. Grow: notes are added back relative to the new root until the scale holds
 *      five or six notes. The choice leans towards the quirky intervals, minor
 *      seconds and the diminished fifth, and away from the major third, sixth
 *      and seventh; the new intervals are what establish the new root by ear.
 *   4. Hold for a few changes, then shrink again.
 *
 * Every change is a single note, or only the root, so the scale never jumps. A
 * scale never holds more than six notes and never three adjacent semitones.
 *
 * This file has no hardware dependencies so it can be tested on a host. It
 * uses rand(), seeded elsewhere.
 */

#ifndef KAIROS_SCALE_H
#define KAIROS_SCALE_H

#include <stdint.h>
#include <stdlib.h>

static const int SCALE_MAX_NOTES      = 6;
static const int SCALE_SKELETON_NOTES = 3;
static const int SCALE_MIN_GROW_TO    = 5; // a grow phase aims for 5 or 6

// Weight of adding each interval above the root while the scale grows. Minor
// second and diminished fifth lead; the major third, sixth and seventh, which
// would make it sound plainly major, are rare.
//                                           R b2  2 b3  3  4 b5  5 b6  6 b7  7
static const uint8_t SCALE_ADD_WEIGHT[12] = {0, 6, 3, 3, 1, 3, 6, 3, 3, 1, 3, 1};

// Order in which intervals are removed while the scale shrinks, higher first:
// thirds carry the major/minor colour, then sixths and sevenths, then the
// seconds and the tritone; fourth and fifth go last.
//                                                R b2  2 b3  3  4 b5  5 b6  6 b7  7
static const uint8_t SCALE_REMOVE_PRIORITY[12] = {0, 2, 2, 4, 4, 1, 2, 1, 3, 3, 3, 3};

// Weight of each interval above the old root as the new root
//                                            R b2  2 b3  3  4 b5  5 b6  6 b7  7
static const uint8_t SCALE_ROOT_WEIGHT[12] = {0, 1, 1, 1, 1, 3, 2, 3, 1, 1, 1, 1};

struct Scale
{
    enum Phase
    {
        PHASE_GROW,
        PHASE_HOLD,
        PHASE_SHRINK,
        PHASE_SHIFT,
    };

    uint16_t mask;     // bit n set: pitch class n (0 = C) is in the scale
    uint8_t  root;     // pitch class of the root, always in the mask
    uint8_t  phase;    // Phase
    uint8_t  growTo;   // size the current grow phase aims for
    uint8_t  holdLeft; // changes left in the hold phase

    static uint16_t Bit(int pc) { return (uint16_t)(1u << (((pc % 12) + 12) % 12)); }

    bool Has(int pc) const { return (mask & Bit(pc)) != 0; }

    int Size() const
    {
        int n = 0;
        for(int pc = 0; pc < 12; pc++)
            n += Has(pc) ? 1 : 0;
        return n;
    }

    /** Would adding `pc` make three adjacent semitones? */
    bool MakesCluster(int pc) const
    {
        bool below2 = Has(pc - 2), below1 = Has(pc - 1);
        bool above1 = Has(pc + 1), above2 = Has(pc + 2);
        return (below2 && below1) || (below1 && above1) || (above1 && above2);
    }

    bool HasThird() const { return Has(root + 3) || Has(root + 4); }

    /** Start on a random root and fifth, grown into a full scale straight away */
    void Init()
    {
        root     = (uint8_t)(rand() % 12);
        mask     = (uint16_t)(Bit(root) | Bit(root + 7));
        phase    = PHASE_GROW;
        growTo   = (uint8_t)(SCALE_MIN_GROW_TO + rand() % 2);
        holdLeft = 0;

        while(phase == PHASE_GROW)
            Change();
    }

    /** Pick an index by weight; -1 if every weight is zero */
    static int PickWeighted(const int* weights)
    {
        int total = 0;
        for(int i = 0; i < 12; i++)
            total += weights[i];
        if(total == 0)
            return -1;

        int pick = rand() % total;
        for(int i = 0; i < 12; i++)
        {
            pick -= weights[i];
            if(pick < 0)
                return i;
        }
        return -1;
    }

    /** Add one note above the root; false if no note may be added */
    bool AddNote()
    {
        if(Size() >= SCALE_MAX_NOTES)
            return false;

        int weights[12];
        for(int i = 0; i < 12; i++)
        {
            int pc     = root + i;
            weights[i] = (Has(pc) || MakesCluster(pc)) ? 0 : SCALE_ADD_WEIGHT[i];
        }

        int interval = PickWeighted(weights);
        if(interval < 0)
            return false;

        mask |= Bit(root + interval);
        return true;
    }

    /** Remove one note other than the root, from the highest priority present */
    void RemoveNote()
    {
        int best = 0;
        for(int i = 1; i < 12; i++)
            if(Has(root + i) && SCALE_REMOVE_PRIORITY[i] > best)
                best = SCALE_REMOVE_PRIORITY[i];

        int weights[12];
        for(int i = 0; i < 12; i++)
            weights[i] = (i != 0 && Has(root + i)
                          && SCALE_REMOVE_PRIORITY[i] == best)
                             ? 1
                             : 0;

        int interval = PickWeighted(weights);
        if(interval >= 0)
            mask &= (uint16_t)~Bit(root + interval);
    }

    /** Move the root to another note of the scale */
    void ShiftRoot()
    {
        int weights[12];
        for(int i = 0; i < 12; i++)
            weights[i] = (i != 0 && Has(root + i)) ? SCALE_ROOT_WEIGHT[i] : 0;

        int interval = PickWeighted(weights);
        if(interval >= 0)
            root = (uint8_t)((root + interval) % 12);
    }

    void StartHold()
    {
        phase    = PHASE_HOLD;
        holdLeft = (uint8_t)(2 + rand() % 3);
    }

    /** One step of the morph: a single note, or only the root, changes */
    void Change()
    {
        switch(phase)
        {
            case PHASE_GROW:
                // Nothing left that may be added ends the growth early
                if(!AddNote() || Size() >= growTo)
                    StartHold();
                break;

            case PHASE_HOLD:
                if(holdLeft > 0)
                    holdLeft--;
                if(holdLeft == 0)
                    phase = PHASE_SHRINK;
                break;

            case PHASE_SHRINK:
                RemoveNote();
                if(Size() <= SCALE_SKELETON_NOTES && !HasThird())
                    phase = PHASE_SHIFT;
                break;

            case PHASE_SHIFT:
                ShiftRoot();
                phase  = PHASE_GROW;
                growTo = (uint8_t)(SCALE_MIN_GROW_TO + rand() % 2);
                break;
        }
    }

    /**
     * @brief Nearest scale note to a position in a note range
     * @param position 0..1 across the range
     * @param base MIDI note at the bottom of the range
     * @param range Width of the range in semitones, at least an octave
     * @return Semitones above `base`, always a scale note within the range
     */
    int Quantize(float position, int base, int range) const
    {
        float target = position * (float)range;

        int   best     = 0;
        float bestDist = 1.0e9f;
        for(int n = 0; n <= range; n++)
        {
            if(!Has(base + n))
                continue;

            float dist = (float)n - target;
            if(dist < 0.0f)
                dist = -dist;

            // Strictly closer only, so a tie goes to the lower note
            if(dist < bestDist)
            {
                bestDist = dist;
                best     = n;
            }
        }
        return best;
    }
};

#endif // KAIROS_SCALE_H
