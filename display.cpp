/**
 * @file display.cpp
 * @brief OLED rendering for Kairos
 */

#include "kairos.h"
#include <stdio.h>

// The grid is narrowed to 3px per step to free the right hand edge for the
// per channel step counts
static const int GRID_CELL_WIDTH = 3;
static const int GRID_ROW_HEIGHT = 9;
static const int GRID_TOP        = 12;
static const int GRID_COUNT_X    = 104;

// Selection marker sits between the grid and the step counts. Hollow means the
// encoder is pointing at the item, filled means turning it edits the item.
static const int MARKER_LEFT  = 97;
static const int MARKER_RIGHT = 101;

// Turing machine blocks below the grid: label, CV bar and gesture bar
static const int TURING_TOP    = 34;
static const int TURING_HEIGHT = 15;
static const int BAR_LEFT      = 44;
static const int BAR_RIGHT     = 127;

// Scale strip in the free space under the first Turing label: one cell per
// pitch class from C, filled when in the scale, the root drawn taller
static const int SCALE_TOP        = 44;
static const int SCALE_CELL_WIDTH = 3;

static void RenderHeader()
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%3d", (int)(tempoBpm + 0.5f));
    hw.display.SetCursor(0, 0);
    hw.display.WriteString(buffer, Font_7x10, true);

    // Tempo selection, underlined rather than marked: the rows below have no
    // space to spare and the header has two free pixel rows
    if(menuItem == MENU_TEMPO)
    {
        if(menuActive)
            hw.display.DrawRect(0, 10, 20, 11, true, true);
        else
            hw.display.DrawLine(0, 11, 20, 11, true);
    }

    // Clock source: patched clock, MIDI master, or our own
    const char* source
        = gateClockMode ? "GTE" : (externalClockMode ? "EXT" : "INT");

    hw.display.SetCursor(24, 2);
    hw.display.WriteString(source, Font_6x8, true);

    hw.display.SetCursor(45, 2);
    hw.display.WriteString(isRunning ? "RUN" : "STP", Font_6x8, true);

    // The transport has no activated state: pressing it acts straight away
    if(menuItem == MENU_TRANSPORT)
        hw.display.DrawLine(45, 11, 62, 11, true);

    // Resync, only there while running. Drawn inverted while it is armed and
    // waiting for the next bar line.
    if(isRunning)
    {
        if(resyncArmed)
            hw.display.DrawRect(65, 1, 83, 9, true, true);

        hw.display.SetCursor(66, 2);
        hw.display.WriteString("SYN", Font_6x8, !resyncArmed);

        if(menuItem == MENU_RESYNC)
            hw.display.DrawLine(66, 11, 83, 11, true);
    }

    snprintf(buffer, sizeof(buffer), "%2d.%d", currentStep / 4 + 1,
             currentStep % 4 + 1);
    hw.display.SetCursor(90, 2);
    hw.display.WriteString(buffer, Font_6x8, true);
}

/**
 * Each channel is a row of 32 cells. A hit is a block whose width shows its
 * gate: one pixel for a short gate, two for a regular one, and a tie carries a
 * low bar through the empty steps it holds. Channel A marks its accents with a
 * line above the hit.
 */
static void RenderKompassGrid()
{
    char buffer[8];

    for(int c = 0; c < NUM_KOMPASS_CHANNELS; c++)
    {
        const KompassChannel& channel = kompass[c];

        int y = GRID_TOP + c * GRID_ROW_HEIGHT;

        for(int s = 0; s < PATTERN_STEPS; s++)
        {
            int x = s * GRID_CELL_WIDTH;

            if(!channel.StepActive(s))
            {
                hw.display.DrawPixel(x, y + 7, true);
                continue;
            }

            bool short_gate
                = channel.shape[channel.Unrotate(s)] == GATE_SHAPE_SHORT;
            hw.display.DrawRect(
                x, y + 2, short_gate ? x : x + 1, y + 7, true, true);

            int tied = channel.TiedSteps(s);
            if(tied > 0)
            {
                // A tie may wrap past the end of the frame; draw what fits
                int end = x + tied * GRID_CELL_WIDTH + 1;
                int last = PATTERN_STEPS * GRID_CELL_WIDTH - 1;
                hw.display.DrawRect(
                    x + 2, y + 6, end < last ? end : last, y + 7, true, true);
            }

            if(c == 0 && channel.StepAccented(s))
                hw.display.DrawLine(x, y, x + 1, y, true);
        }

        if(menuItem == MENU_PATTERN_1 + c)
            hw.display.DrawRect(MARKER_LEFT, y + 2, MARKER_RIGHT, y + 6, true,
                                menuActive);

        // How many of the 32 steps this channel is firing, so a density can be
        // dialled to an exact step count instead of by eye
        snprintf(buffer, sizeof(buffer), "%2d", channel.ActiveSteps());
        hw.display.SetCursor(GRID_COUNT_X, y + 1);
        hw.display.WriteString(buffer, Font_6x8, true);
    }

    // Playhead under the grid
    int px = currentStep * GRID_CELL_WIDTH;
    int py = GRID_TOP + NUM_KOMPASS_CHANNELS * GRID_ROW_HEIGHT;
    hw.display.DrawRect(px, py, px + 1, py + 1, true, true);
}

/**
 * Per Turing machine: loop length, the held voltage as a bar, and under it the
 * level of the write gesture on its knob
 */
static void RenderTuring()
{
    char buffer[16];

    for(int t = 0; t < NUM_TURING_MACHINES; t++)
    {
        int y = TURING_TOP + t * TURING_HEIGHT;

        // Loop length, showing the target while a length morph is in progress
        if(turing[t].pendingLength)
            snprintf(buffer, sizeof(buffer), "%d:%d>%d", t + 1,
                     turing[t].length, turing[t].pendingLength);
        else
            snprintf(buffer, sizeof(buffer), "%d:%d", t + 1, turing[t].length);

        hw.display.SetCursor(0, y);
        hw.display.WriteString(buffer, Font_6x8, true);

        // Outline plus fill proportional to the held voltage
        hw.display.DrawRect(BAR_LEFT, y, BAR_RIGHT, y + 6, true, false);

        int width = (int)(cvOut[t] * (float)(BAR_RIGHT - BAR_LEFT - 2));
        if(width > 0)
            hw.display.DrawRect(BAR_LEFT + 1, y + 1, BAR_LEFT + 1 + width,
                                y + 5, true, true);

        // Gesture level: how strongly this machine is being rewritten
        int activity
            = (int)(turing[t].activity * (float)(BAR_RIGHT - BAR_LEFT));
        if(activity > 0)
            hw.display.DrawRect(BAR_LEFT, y + 9, BAR_LEFT + activity, y + 10,
                                true, true);
    }
}

static void RenderScale()
{
    for(int pc = 0; pc < 12; pc++)
    {
        int x = pc * SCALE_CELL_WIDTH;

        if(pc == scale.root)
            hw.display.DrawRect(x, SCALE_TOP - 1, x + 1, SCALE_TOP + 3, true,
                                true);
        else if(scale.Has(pc))
            hw.display.DrawRect(x, SCALE_TOP, x + 1, SCALE_TOP + 2, true, true);
        else
            hw.display.DrawPixel(x, SCALE_TOP + 2, true);
    }
}

void UpdateDisplay()
{
    hw.display.Fill(false);

    RenderHeader();
    RenderKompassGrid();
    RenderTuring();
    RenderScale();

    hw.display.Update();
}
