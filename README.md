# Kairos

MIDI clock master/slave and generative gate source for the **Daisy Patch**.

## I/O

| Jack | Function |
|------|----------|
| OUT1 | Channel A gate (downbeat character) |
| OUT2 | Channel A accent |
| OUT3 | Channel B gate (backbeat character) |
| OUT4 | 16th note clock |
| GATE OUT | Reset pulse, fired just before the transport starts or on a resync |
| CV1 | Turing machine 1, sampled on channel A hits (0-5V) |
| CV2 | Turing machine 2, sampled on channel B hits (0-5V) |
| GATE IN1 | Analog clock in, one pulse per step (16th note) |
| GATE IN2 | Reset in |
| CTRL1 | Density of channel A (re-rolls the steps not playing) |
| CTRL2 | Write gesture for Turing machine 1 (see below) |
| CTRL3 | Density of channel B (re-rolls the steps not playing) |
| CTRL4 | Write gesture for Turing machine 2 |
| Encoder turn | Choose a menu item, or edit the active one |
| Encoder press | Activate / leave the item, or start/stop on the transport |
| MIDI IN | Clock, Start, Continue, Stop, Reset, Song Position Pointer |
| MIDI OUT | Clock and transport, generated or forwarded |

Gates are 5V and vary in length (see *Gate lengths*). They rise 1ms after their step,
so the CV has settled and a gate is always seen as a new one. The clock pulse on OUT4 is
10ms, the reset pulse 20ms.

## Menu

The encoder is both the menu and the transport. Turning it moves between five items:
tempo, transport, resync, and the two Kompass patterns. A press activates the item under the
highlight; turning then edits it. Another press leaves it and the encoder goes back to
moving between items.

- **Tempo**: 20-300 BPM, one BPM per detent. The tempo is kept in flash and comes
  back after a power cycle. It is saved a second after the last change, whether the
  sequencer is running or stopped, so sweeping the encoder costs one write. A tempo
  derived from a gate clock is never stored.

  Saving is safe while running because the tempo is an append-only log in one 4KB
  QSPI sector: each save programs 8 bytes into already erased space, which stalls the
  main loop for about a millisecond. Only compacting the log needs a sector erase,
  which can stall it for up to 300ms, so that happens at boot when the log is more
  than half full, or, if 512 saves fill it within one session, at the next stop. A
  tempo saved by the previous firmware is carried over. Under a MIDI or gate clock the tempo is
  not ours to set, so the item is skipped entirely; if the menu is sitting on it, or on
  the transport, when an external clock arrives, it drops back to pattern 1.
- **Transport**: the exception, because there is nothing to turn once it is chosen. Its
  press starts and stops the sequencer straight away instead of activating anything, so
  it has no activated state. Like the tempo it belongs to the clock master, so it is
  skipped under a MIDI or gate clock.
- **Resync** (`SYN`): only there while the sequencer runs, under any clock source. A
  press arms it and the label is drawn inverted; a second press cancels it. On the
  next bar line (step 1, 17 of the 32 step pattern) the reset pulse goes out on GATE
  OUT and MIDI out gets a Stop and a Start, both just ahead of that downbeat's clock,
  so downstream gear plays its first step on it. Kairos keeps playing and keeps its
  own position. Like the transport it acts on the press and has no activated state.
  If the sequencer stops while the menu sits on it, the menu falls back to the
  transport.
- **Pattern A/B**: rotates that Kompass channel forwards or backwards in time, one step
  per detent, wrapping at 32. Only the mapping from time to ranking moves, so a shift
  costs nothing, never changes how many steps fire, and survives both the drift and the
  density control.

The display shows the state: `RUN`/`STP` and the tempo are underlined when selected, the
tempo gets a solid bar when activated, `SYN` is underlined when selected and inverted
while armed, and each pattern row gets a marker to the left of
its step count, hollow when selected and filled when activated.

## Clock

Three sources, in priority order: a clock patched into GATE IN 1, MIDI clock in, and
the internal clock. All three drive MIDI out with 24 PPQN clock and transport, so
Kairos is always a usable clock master for the rest of the rig.

- **Gate**: one pulse on GATE IN 1 is one pattern step, so four pulses make a quarter
  note. The interval between pulses sets the tempo, and the internal 24 PPQN generator
  runs at that tempo to fill in the five MIDI clock bytes between two edges. Steps are
  only ever advanced by an edge, so the pattern cannot drift away from the master even
  if the derived tempo is momentarily off; if the generator runs ahead it just waits
  for the next edge rather than sending a 25th clock. Intervals outside the 20-300 BPM
  range are ignored as glitches. The transport starts on the first pulse and a Start is
  sent to MIDI out. While a gate clock is running, incoming MIDI clock and transport
  are ignored, and the encoder no longer sets tempo or starts/stops. With no pulse for
  1.5s Kairos leaves gate mode, sends a Stop and reverts to the internal clock.
- **Reset**: a pulse on GATE IN 2 rewinds the pattern so the next clock plays step 0,
  echoes the reset on GATE OUT and sends Song Position 0 to MIDI out. It works under
  any clock source.
- **External**: any incoming MIDI clock switches Kairos to external mode. Clock and
  transport messages are forwarded to MIDI out unchanged, so Kairos can sit in the
  middle of a chain. A master that only sends clock without a Start message still
  runs the sequencer; after a Stop its clock is forwarded but ignored until the next
  Start or Continue.
- **Internal**: with no external clock for 500ms Kairos reverts to internal mode and
  sends a Stop. The encoder then sets tempo and starts/stops the sequencer, sending
  Start/Stop and 24 PPQN clock to MIDI out.
- Song Position Pointer relocates the pattern to the matching 16th note.
- The reset pulse on GATE OUT goes out immediately before the transport starts, so
  downstream sequencers are already rewound when the first clock pulse arrives. It
  fires on an internal start, on MIDI Start, on the first clock of an external master
  that sends no Start, and on MIDI Reset. Continue does not reset, since it is meant
  to resume from the current position.

## Kompass channels

There are two channels: A with a downbeat character, strong metric positions first, and
B with a backbeat character, beats 2 and 4 first. Each ranks all 32 steps by a weight
table that reflects its character. The density control decides how many of those steps
may fire, lowest rank first, so turning density up adds hits to the pattern that is
already playing instead of generating a new one.

The number to the right of each row on the display is how many of the 32 steps that
channel is currently firing, so a density can be dialled to an exact step count rather
than set by eye.

Each weight is a fixed character value plus two offsets: one that drifts slowly on its
own and one that the density control re-rolls.

**Drift.** A few steps per pattern cycle are random-walked within a clamped range, so
the ranking keeps rearranging itself and the channel grows genuinely different patterns
while never losing its character. It is deliberately slow: measured over 300 runs, a
channel sitting at 8 steps first changes a hit after 8 to 14 cycles, so roughly half a
minute at 120 BPM, and keeps evolving from there. The drift also starts from a random
position, so no two boots play the same thing.

**Density.** Every change of the step count, down to a single step, re-rolls the steps
that are *not* currently firing and ranks them again. The steps that survive the change
keep their ranks, so the control can only ever add or take away hits, never move one:
turning density up adds a hit without disturbing the pattern underneath it. But which
hit gets added is queued up fresh each time, so sweeping down and back up returns a
different pattern in the same character rather than the one just left. Dropping a
channel to zero and bringing it back is the quick way to ask for a new pattern.

The re-roll range is smaller than the gap between a channel's leading step and its
weakest one, so a channel always keeps its strongest step however often the density is
swept, while steps in neighbouring tiers trade places freely. A channel whose active
hits are all one tier therefore changes less than one sitting on a tier boundary.

The step count uses a small hysteresis, so control noise sitting on a boundary cannot
reshuffle a channel. The step count also uses the shared control margin described under
*Controls*, so an empty and a completely full pattern are reachable on every channel.

**Accents.** OUT2 accents channel A on its strongest hits, the top quarter of its ranking,
rounded up, so a single hit is always accented. An accent gate lasts as long as the
note it accents. The accents follow the ranking, so they move with the drift and the
density control like the hits do.

## Gate lengths

Every step carries a gate shape as part of the pattern, so a hit plays the same length
every cycle:

- **Short**, about a fifth of the steps: a third of a step.
- **Tie**, about a third: the gate holds through up to 1 to 3 following empty steps and
  drops halfway through the last one, so the next hit always starts from a low gate. A
  tie stops at the next hit, so it only ever fills a gap. With no empty step after it, it
  plays as a regular gate. Ties therefore appear as the density goes down, and a full
  pattern is plain 16ths.
- **Regular**, the rest: half a step.

Lengths are counted in clock pulses, 6 to a step, so they follow the tempo under every
clock source. The drift re-rolls one step's shape per pattern cycle, and moving the
density re-rolls the shapes of the steps not playing, so the shapes evolve along with
the hits.

The display draws a hit one pixel wide for a short gate and two for a regular one; a tie
adds a low bar through the steps it holds, and an accent puts a line above the hit.

## Turing machines

Two independent sequences living in the same 32 step frame as the gates, one 8-bit
value per step. The loop length is 8, 16 or 32 steps; at length 8 the frame holds four
repetitions of the same 8 steps.

The loops never change on their own. Each is driven by a gesture on its own knob, CTRL2
for machine 1 and CTRL4 for machine 2:

- **Moving** the knob raises the chance that a step gets rewritten. Any deliberate
  movement immediately buys a usable write chance instead of having to build one up.
  Stop moving and that chance fades out over about a second, which locks the loop
  again. Parking the knob anywhere, at any value, changes nothing.
  Movement is measured over a 20ms window rather than per control tick: at the 1kHz
  control rate a normal sweep moves the knob by less than the ADC noise floor within a
  single tick, so a per-tick threshold only ever caught a flick of the wrist.
- **Where** the knob is decides what gets written. Sweeping through low values paints low
  voltages into the sequence, sweeping up high paints high ones, with a small spread
  around the knob position so a sweep lays down a band rather than one flat level. The
  position uses the shared control margin so the ends of the CV range are reachable,
  while the movement that drives the gesture is measured on the raw travel, so a gesture
  at either extreme still registers.
- Rewriting a step rewrites its whole loop position, so at length 8 all four repetitions
  of that step follow along.
- Length changes are rolled from the same gesture level, so lengths only move while you
  are moving the knob. A change already in flight finishes its frame.

A length change is applied one step at a time as the playhead sweeps one frame, so
shrinking pulls the later repetitions onto the earlier ones step by step, and growing
lets the repetitions drift apart from that point on. The display shows the current
length, `8>16` while a morph is in flight, and the gesture level as a bar under the
machine's CV bar. The
clock source is shown in the header as `GTE`, `EXT` or `INT`.

The voltages are not sent on every step. Machine 1 is sampled and held on channel A hits
and machine 2 on channel B hits, so each CV only moves when its paired channel fires. The
machines still advance every step, which keeps them locked to the 32 step frame and to
the gate pattern.

## Controls

The panel controls do not all reach the ends of their travel: CTRL1 stops around 96% and
would leave a pattern one step short of full. The outer 5% at each end of every control
is therefore folded onto the ends of its range, so 0% and 100% are reachable everywhere.
It costs a little resolution in the middle, which is nothing on a density or a gesture
control, and both ends now behave identically on all four.

## Build

```bash
make clean && make
make program-dfu
```

Flash usage: 81.1% of 128KB.
