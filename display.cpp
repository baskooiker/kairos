/**
 * @file display.cpp
 * @brief OLED rendering for Kairos
 */

#include "kairos.h"
#include <stdio.h>

// The grid is narrowed to 3px per step to free the right hand edge for the
// per channel step counts
static const int GRID_CELL_WIDTH = 3;
static const int GRID_ROW_HEIGHT = 8;
static const int GRID_TOP        = 12;
static const int GRID_COUNT_X    = 104;

// Selection marker sits between the grid and the step counts. Hollow means the
// encoder is pointing at the item, filled means turning it edits the item.
static const int MARKER_LEFT  = 97;
static const int MARKER_RIGHT = 101;

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

static void RenderKompassGrid()
{
    char buffer[8];

    for(int c = 0; c < NUM_KOMPASS_CHANNELS; c++)
    {
        int y = GRID_TOP + c * GRID_ROW_HEIGHT;

        for(int s = 0; s < PATTERN_STEPS; s++)
        {
            int  x      = s * GRID_CELL_WIDTH;
            bool active = kompass[c].StepActive(s);

            if(active)
                hw.display.DrawRect(x, y, x + 1, y + 5, true, true);
            else
                hw.display.DrawPixel(x, y + 5, true);
        }

        if(menuItem == MENU_PATTERN_1 + c)
            hw.display.DrawRect(MARKER_LEFT, y + 1, MARKER_RIGHT, y + 5, true,
                                menuActive);

        // How many of the 32 steps this channel is firing, so a density can be
        // dialled to an exact step count instead of by eye
        snprintf(buffer, sizeof(buffer), "%2d", kompass[c].ActiveSteps());
        hw.display.SetCursor(GRID_COUNT_X, y);
        hw.display.WriteString(buffer, Font_6x8, true);
    }

    // Playhead under the grid
    int px = currentStep * GRID_CELL_WIDTH;
    hw.display.DrawRect(px, GRID_TOP + NUM_KOMPASS_CHANNELS * GRID_ROW_HEIGHT,
                        px + 1,
                        GRID_TOP + NUM_KOMPASS_CHANNELS * GRID_ROW_HEIGHT + 1,
                        true, true);
}

/** CTRL4 gesture level: how strongly the Turing machines are being rewritten */
static void RenderActivity()
{
    hw.display.SetCursor(0, 38);
    hw.display.WriteString("W", Font_6x8, true);

    int width = (int)(knobActivity * 110.0f);
    if(width > 0)
        hw.display.DrawRect(10, 40, 10 + width, 43, true, true);
    else
        hw.display.DrawLine(10, 43, 120, 43, true);
}

static void RenderCvBars()
{
    char buffer[16];

    for(int t = 0; t < NUM_TURING_MACHINES; t++)
    {
        int y = 46 + t * 9;

        // Loop length, showing the target while a length morph is in progress
        if(turing[t].pendingLength)
            snprintf(buffer, sizeof(buffer), "%d:%d>%d", t + 1,
                     turing[t].length, turing[t].pendingLength);
        else
            snprintf(buffer, sizeof(buffer), "%d:%d", t + 1, turing[t].length);

        hw.display.SetCursor(0, y);
        hw.display.WriteString(buffer, Font_6x8, true);

        // Outline plus fill proportional to the held voltage
        int left = 44;
        hw.display.DrawRect(left, y, 127, y + 6, true, false);

        int width = (int)(cvOut[t] * (float)(127 - left - 2));
        if(width > 0)
            hw.display.DrawRect(left + 1, y + 1, left + 1 + width, y + 5, true,
                                true);
    }
}

void UpdateDisplay()
{
    hw.display.Fill(false);

    RenderHeader();
    RenderKompassGrid();
    RenderActivity();
    RenderCvBars();

    hw.display.Update();
}
