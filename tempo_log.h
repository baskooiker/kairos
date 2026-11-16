/**
 * @file tempo_log.h
 * @brief Append-only tempo log in one erased flash sector
 *
 * Erasing a QSPI sector stalls the main loop for 70 to 300ms, but programming a
 * few bytes into space that is already erased takes well under a millisecond.
 * The tempo is therefore stored as a log: every save appends a small record to
 * the erased part of the sector, and the newest valid record is the tempo. The
 * sector is only erased when the log is compacted, which happens at boot or
 * while the sequencer is stopped, so a save never stalls a running clock.
 *
 * This file has no hardware dependencies so the log logic can be tested on a
 * host.
 */

#ifndef KAIROS_TEMPO_LOG_H
#define KAIROS_TEMPO_LOG_H

#include <stdint.h>
#include <string.h>

static const uint32_t TEMPO_LOG_SECTOR_SIZE = 4096;

/** One log entry: the tempo and a check word that ties it to Kairos */
struct TempoLogRecord
{
    uint32_t bits;  // the tempo as the bit pattern of a float
    uint32_t check; // bits ^ TEMPO_LOG_MAGIC
};

static const uint32_t TEMPO_LOG_MAGIC = 0x4B414952; // "KAIR"
static const uint32_t TEMPO_LOG_RECORDS
    = TEMPO_LOG_SECTOR_SIZE / sizeof(TempoLogRecord);

// Erased flash reads as all ones
static const uint32_t TEMPO_LOG_ERASED = 0xFFFFFFFF;

// The previous firmware kept the tempo in libDaisy's PersistentStorage at the
// same address: a State word followed by the settings. State::USER is 2.
static const uint32_t TEMPO_LOG_LEGACY_USER_STATE = 2;

/** What a scan of the sector found */
struct TempoLogScan
{
    bool     found;    // a valid tempo was found
    float    tempo;    // the newest valid tempo, if found
    uint32_t nextFree; // index of the first record after the last used one
    bool     foreign;  // the sector holds data that is not a log
};

static inline uint32_t TempoLogFloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline float TempoLogBitsFloat(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline TempoLogRecord TempoLogMakeRecord(float tempo)
{
    TempoLogRecord record;
    record.bits  = TempoLogFloatBits(tempo);
    record.check = record.bits ^ TEMPO_LOG_MAGIC;
    return record;
}

static inline bool TempoInRange(float tempo, float minBpm, float maxBpm)
{
    // Written this way round so that NaN is out of range as well
    return tempo >= minBpm && tempo <= maxBpm;
}

/**
 * @brief Find the newest tempo in a sector image
 *
 * Records that fail the check, such as one torn by a power cut halfway through
 * a write, are skipped but still count as used, so the next write goes after
 * them. A sector that does not start with a log record is foreign: it is either
 * the previous firmware's PersistentStorage data, whose tempo is adopted, or
 * something unknown, and either way the sector has to be erased before use.
 */
static inline TempoLogScan ScanTempoLog(const TempoLogRecord* records,
                                        float                 minBpm,
                                        float                 maxBpm)
{
    TempoLogScan scan;
    scan.found    = false;
    scan.tempo    = 0.0f;
    scan.nextFree = 0;
    scan.foreign  = false;

    const TempoLogRecord& first = records[0];

    if(first.bits == TEMPO_LOG_ERASED && first.check == TEMPO_LOG_ERASED)
        return scan; // empty sector, nothing stored yet

    if((first.bits ^ TEMPO_LOG_MAGIC) != first.check)
    {
        scan.foreign = true;
        scan.nextFree = TEMPO_LOG_RECORDS;

        float legacy = TempoLogBitsFloat(first.check);
        if(first.bits == TEMPO_LOG_LEGACY_USER_STATE
           && TempoInRange(legacy, minBpm, maxBpm))
        {
            scan.found = true;
            scan.tempo = legacy;
        }
        return scan;
    }

    for(uint32_t i = 0; i < TEMPO_LOG_RECORDS; i++)
    {
        const TempoLogRecord& record = records[i];

        if(record.bits == TEMPO_LOG_ERASED && record.check == TEMPO_LOG_ERASED)
            continue;

        scan.nextFree = i + 1;

        if((record.bits ^ TEMPO_LOG_MAGIC) != record.check)
            continue;

        float tempo = TempoLogBitsFloat(record.bits);
        if(!TempoInRange(tempo, minBpm, maxBpm))
            continue;

        scan.found = true;
        scan.tempo = tempo;
    }

    return scan;
}

#endif // KAIROS_TEMPO_LOG_H
