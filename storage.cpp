/**
 * @file storage.cpp
 * @brief Settings kept in QSPI flash across power cycles
 *
 * Only the tempo set with the encoder is stored. A tempo derived from a gate
 * clock is not ours to keep, so it is never written.
 *
 * The tempo lives in an append-only log (see tempo_log.h), so a save is a page
 * program of a few bytes: under a millisecond, which is safe while the
 * sequencer runs. The sector erase that compacts the log stalls the main loop
 * for up to 300ms, so it only happens at boot, or once the log is full and the
 * sequencer is stopped.
 */

#include "kairos.h"
#include "tempo_log.h"

// Start of the log on the QSPI chip, sector aligned. Kairos runs from internal
// flash, so nothing else lives on the chip.
static const uint32_t LOG_ADDRESS = 0;

// How long the tempo has to stay put before it is written, so sweeping the
// encoder costs a single record instead of one per detent
static const uint32_t SAVE_DELAY_MS = 1000;

// At boot, a log fuller than this is compacted so a session never starts close
// to the point where saving has to wait for a stop
static const uint32_t COMPACT_AT_BOOT_RECORDS = TEMPO_LOG_RECORDS / 2;

static bool     saveTempoPending = false;
static float    savedTempoBpm    = DEFAULT_BPM; // last tempo set by the user
static uint32_t lastTempoEditMs  = 0;
static uint32_t nextFreeRecord   = 0;

static uint32_t RecordAddress(uint32_t index)
{
    return LOG_ADDRESS + index * sizeof(TempoLogRecord);
}

static void AppendRecord(float tempo)
{
    TempoLogRecord record = TempoLogMakeRecord(tempo);
    hw.seed.qspi.Write(
        RecordAddress(nextFreeRecord), sizeof(record), (uint8_t*)&record);
    nextFreeRecord++;
}

/** Erase the sector and start the log over with a single record */
static void CompactLog(float tempo)
{
    hw.seed.qspi.EraseSector(LOG_ADDRESS);
    nextFreeRecord = 0;
    AppendRecord(tempo);
}

void LoadSettings()
{
    TempoLogRecord* records
        = reinterpret_cast<TempoLogRecord*>(hw.seed.qspi.GetData(LOG_ADDRESS));

    // The QSPI window may be cached; make sure the scan sees the chip
    dsy_dma_invalidate_cache_for_buffer((uint8_t*)records,
                                        TEMPO_LOG_SECTOR_SIZE);

    TempoLogScan scan = ScanTempoLog(records, MIN_BPM, MAX_BPM);

    float tempo    = scan.found ? scan.tempo : DEFAULT_BPM;
    tempoBpm       = tempo;
    savedTempoBpm  = tempo;
    nextFreeRecord = scan.nextFree;

    // Booting is the one moment an erase stalls nothing
    if(scan.foreign || scan.nextFree > COMPACT_AT_BOOT_RECORDS)
        CompactLog(tempo);
}

void OnTempoEdited()
{
    savedTempoBpm    = tempoBpm;
    saveTempoPending = true;
    lastTempoEditMs  = System::GetNow();
}

void ProcessSettingsSave()
{
    if(!saveTempoPending)
        return;

    if(System::GetNow() - lastTempoEditMs < SAVE_DELAY_MS)
        return;

    if(nextFreeRecord < TEMPO_LOG_RECORDS)
    {
        AppendRecord(savedTempoBpm);
    }
    else
    {
        // The log is full and compacting it means an erase, which would stall
        // a running clock: keep the save pending until the sequencer stops
        if(isRunning)
            return;

        CompactLog(savedTempoBpm);
    }

    saveTempoPending = false;
}
