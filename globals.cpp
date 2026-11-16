/**
 * @file globals.cpp
 * @brief Definitions of all global state
 */

#include "kairos.h"

DaisyPatch hw;

float    tempoBpm            = DEFAULT_BPM;
bool     isRunning           = false;
bool     externalClockMode   = false;
uint32_t lastExternalPulseMs = 0;
bool     externalStopped     = false;
int      pulseInStep         = 0;
int      currentStep         = 0;
bool     resyncArmed         = false;

bool     gateClockMode         = false;
uint32_t lastGatePulseMs       = 0;
bool     gateAwaitingFirstStep = false;
int      pulsesSinceGateEdge   = 0;

volatile uint32_t internalPulsesGenerated = 0;
uint32_t          internalPulsesConsumed  = 0;
float             samplesPerPulse         = 1000.0f;
volatile bool     pulsePhaseReset         = false;

volatile bool     triggerOut[NUM_KOMPASS_CHANNELS]     = {false, false, false};
volatile uint32_t triggerCounter[NUM_KOMPASS_CHANNELS] = {0, 0, 0};
volatile bool     clockOut                             = false;
volatile uint32_t clockOutCounter                      = 0;
volatile bool     resetOut                             = false;
volatile uint32_t resetOutCounter                      = 0;

int  menuItem   = MENU_TRANSPORT;
bool menuActive = false;

float knobActivity  = 0.0f;
float lastKnobValue = 0.0f;

KompassChannel kompass[NUM_KOMPASS_CHANNELS];
TuringMachine  turing[NUM_TURING_MACHINES];
float          cvOut[NUM_TURING_MACHINES] = {0.0f, 0.0f};
