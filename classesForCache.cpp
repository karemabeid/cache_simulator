#include <cmath>
#include <cstdint>
#include <cstdlib>


static unsigned extractSetIndex(uint32_t addr, unsigned blkBits, unsigned numSets) {
    if (numSets <= 1) {
        return 0;
    }
    unsigned setBits = static_cast<unsigned>(std::log2(numSets));
    return (addr >> blkBits) & ((1u << setBits) - 1);
}

static uint32_t extractTag(uint32_t addr, unsigned blkBits, unsigned numSets) {
    unsigned setBits = (numSets <= 1 ? 0u : static_cast<unsigned>(std::log2(numSets)));
    unsigned shiftAmt = blkBits + setBits;
    if (shiftAmt >= 32u) {
        return 0u;
    }
    return (addr >> shiftAmt);
}

static uint32_t rebuildBlockAddress(uint32_t tag, unsigned setIdx, unsigned blkBits, unsigned numSets) {
    unsigned setBits = (numSets <= 1 ? 0u : static_cast<unsigned>(std::log2(numSets)));
    return (tag << (setBits + blkBits)) | (setIdx << blkBits);
}

enum AllocPolicy {
    ALLOC_WRITE_ALLOC   = 1,
    ALLOC_NO_WRITE_ALLOC = 0
};

class CacheLine {
public:
    bool    valid;
    bool    dirty;
    uint32_t tag;
    int     lruRank;

    CacheLine()
            : valid(false), dirty(false), tag(static_cast<uint32_t>(-1)), lruRank(0)
    {}

    void reset() {
        valid   = false;
        dirty   = false;
        tag     = static_cast<uint32_t>(-1);
        lruRank = 0;
    }
};


class LevelCache {
public:
    unsigned     blkBits;
    unsigned     sizeBits;
    unsigned     cycTicks;
    AllocPolicy  policy;
    unsigned     numWays;
    unsigned     totalBlocks;
    unsigned     numSets;

    CacheLine**  lines;
    unsigned     accessCnt;
    unsigned     missCnt;

    LevelCache(unsigned blkBits_,
               unsigned sizeBits_,
               unsigned cycTicks_,
               unsigned policyFlag,
               unsigned assocBits_)
            : blkBits(blkBits_),
              sizeBits(sizeBits_),
              cycTicks(cycTicks_),
              policy(static_cast<AllocPolicy>(policyFlag)),
              numWays(static_cast<unsigned>(std::pow(2, assocBits_))),
              accessCnt(0),
              missCnt(0)
    {
        totalBlocks = static_cast<unsigned>(std::pow(2, (sizeBits - blkBits)));
        numSets     = totalBlocks / numWays;
        lines = new CacheLine*[numSets];
        for (unsigned s = 0; s < numSets; ++s) {
            lines[s] = new CacheLine[numWays];
            for (unsigned w = 0; w < numWays; ++w) {
                lines[s][w].reset();
            }
        }
    }

    ~LevelCache() {
        for (unsigned s = 0; s < numSets; ++s) {
            delete[] lines[s];
        }
        delete[] lines;
    }

    void promoteLRU(unsigned setIdx, unsigned wayIdx) {
        int oldRank = lines[setIdx][wayIdx].lruRank;
        lines[setIdx][wayIdx].lruRank = static_cast<int>(numWays - 1);
        for (unsigned w = 0; w < numWays; ++w) {
            if (w != wayIdx && lines[setIdx][w].lruRank > oldRank) {
                lines[setIdx][w].lruRank--;
            }
        }
    }

    unsigned pickEvictWay(unsigned setIdx) const {
        for (unsigned w = 0; w < numWays; ++w) {
            if (lines[setIdx][w].lruRank == 0) {
                return w;
            }
        }
        return 0;
    }

    bool readAccess(uint32_t addr) {
        accessCnt++;
        unsigned setIdx = extractSetIndex(addr, blkBits, numSets);
        uint32_t tg     = extractTag(addr, blkBits, numSets);

        for (unsigned w = 0; w < numWays; ++w) {
            CacheLine& ln = lines[setIdx][w];
            if (ln.valid && ln.tag == tg) {
                promoteLRU(setIdx, w);
                return true;
            }
        }
        return false;
    }

    bool writeAccess(uint32_t addr, bool isEvictRef) {
        if (!isEvictRef) {
            accessCnt++;
        }
        unsigned setIdx = extractSetIndex(addr, blkBits, numSets);
        uint32_t tg     = extractTag(addr, blkBits, numSets);

        for (unsigned w = 0; w < numWays; ++w) {
            CacheLine& ln = lines[setIdx][w];
            if (ln.valid && ln.tag == tg) {
                ln.dirty = true;
                promoteLRU(setIdx, w);
                return true;
            }
        }
        return false;
    }

    void loadBlock(uint32_t addr,
                   int32_t* evictedOut,
                   bool isSecondLevel,
                   LevelCache* lowerLevel,
                   char opType,
                   bool fillFromLower)
    {
        unsigned setIdx = extractSetIndex(addr, blkBits, numSets);
        uint32_t tg     = extractTag(addr, blkBits, numSets);

        // 1) If any invalid line, fill it
        for (unsigned w = 0; w < numWays; ++w) {
            CacheLine& ln = lines[setIdx][w];
            if (!ln.valid) {
                ln.valid = true;
                ln.tag   = tg;
                ln.dirty = (opType == 'w' && !fillFromLower);
                promoteLRU(setIdx, w);
                *evictedOut = -1;
                return;
            }
        }

        // 2) All lines valid → evict LRU
        unsigned victimWay = pickEvictWay(setIdx);
        CacheLine& victim = lines[setIdx][victimWay];

        bool wasDirty   = victim.dirty;
        uint32_t oldTag = victim.tag;
        uint32_t evAddr = rebuildBlockAddress(
                oldTag,
                setIdx,
                blkBits,
                numSets
        );
        // Install new block
        victim.tag   = tg;
        victim.valid = true;
        victim.dirty = (opType == 'w' && !fillFromLower);
        promoteLRU(setIdx, victimWay);

        *evictedOut = static_cast<int32_t>(evAddr);
        // If we evicted a dirty line from L1, write it back to L2
        if (wasDirty && !isSecondLevel) {
            lowerLevel->writeAccess(evAddr, true);
        }
    }


    void invalidate(uint32_t addr) {
        unsigned setIdx = extractSetIndex(addr, blkBits, numSets);
        uint32_t tg     = extractTag(addr, blkBits, numSets);
        for (unsigned w = 0; w < numWays; ++w) {
            CacheLine& ln = lines[setIdx][w];
            if (ln.valid && ln.tag == tg) {
                ln.valid   = false;
                ln.dirty   = false;
                ln.tag     = static_cast<uint32_t>(-1);
                ln.lruRank = 0;
                return;
            }
        }
    }
};


class CacheManager {
public:
    LevelCache firstLevel;
    LevelCache secondLevel;
    unsigned    memCycles;
    double      totalCycles;

    CacheManager(unsigned blkBits,
                 unsigned wrAlloc,
                 unsigned memCyc,
                 unsigned l1SizeBits,
                 unsigned l1AssocBits,
                 unsigned l1Cyc,
                 unsigned l2SizeBits,
                 unsigned l2AssocBits,
                 unsigned l2Cyc)
            : firstLevel(blkBits, l1SizeBits, l1Cyc, wrAlloc, l1AssocBits),
              secondLevel(blkBits, l2SizeBits, l2Cyc, wrAlloc, l2AssocBits),
              memCycles(memCyc),
              totalCycles(0.0)
    {}

    ~CacheManager() = default;


    void access(uint32_t addr, char op) {
        int32_t evictedAddr = -1;

        if (op == 'r') {
            // 1) Try L1 read
            bool hitL1 = firstLevel.readAccess(addr);
            totalCycles += static_cast<double>(firstLevel.cycTicks);
            if (hitL1) return;

            // L1 miss
            firstLevel.missCnt++;
            // 2) Try L2 read
            bool hitL2 = secondLevel.readAccess(addr);
            totalCycles += static_cast<double>(secondLevel.cycTicks);
            if (!hitL2) {
                // L2 miss → main memory
                secondLevel.missCnt++;
                totalCycles += static_cast<double>(memCycles);
                // Load into L2
                secondLevel.loadBlock(addr, &evictedAddr, true,
                                      &secondLevel, 'r', false);
                if (evictedAddr != -1) {
                    firstLevel.invalidate(static_cast<uint32_t>(evictedAddr));
                }
                // Load into L1
                firstLevel.loadBlock(addr, &evictedAddr, false,
                                     &secondLevel, 'r', true);
            } else {
                // L2 hit → fetch into L1
                firstLevel.loadBlock(addr, &evictedAddr, false,
                                     &secondLevel, 'r', true);
            }
        }
        else if (op == 'w') {
            // 1) Try L1 write
            bool hitL1 = firstLevel.writeAccess(addr, false);
            totalCycles += static_cast<double>(firstLevel.cycTicks);
            if (hitL1) return;

            // L1 miss
            firstLevel.missCnt++;
            // 2) Try L2 write
            bool hitL2 = secondLevel.writeAccess(addr, false);
            totalCycles += static_cast<double>(secondLevel.cycTicks);
            if (hitL2) {
                // On L2 hit, if Write‐Allocate, fetch into L1
                if (secondLevel.policy == ALLOC_WRITE_ALLOC) {
                    firstLevel.loadBlock(addr, &evictedAddr, false,
                                         &secondLevel, 'w', true);
                }
                return;
            }

            // L2 miss → main memory
            secondLevel.missCnt++;
            totalCycles += static_cast<double>(memCycles);
            if (secondLevel.policy == ALLOC_WRITE_ALLOC) {
                // Allocate in L2, then allocate in L1
                secondLevel.loadBlock(addr, &evictedAddr, true,
                                      &secondLevel, 'w', false);
                if (evictedAddr != -1) {
                    firstLevel.invalidate(static_cast<uint32_t>(evictedAddr));
                }
                firstLevel.loadBlock(addr, &evictedAddr, false,
                                     &secondLevel, 'w', true);
            }

        }
    }

    void finalizeStats(double& l1MissRate, double& l2MissRate, double& avgTime) const {
        if (firstLevel.accessCnt > 0) {
            l1MissRate = static_cast<double>(firstLevel.missCnt)
                         / static_cast<double>(firstLevel.accessCnt);
            avgTime    = totalCycles / static_cast<double>(firstLevel.accessCnt);
        } else {
            l1MissRate = 0.0;
            avgTime    = 0.0;
        }
        if (secondLevel.accessCnt > 0) {
            l2MissRate = static_cast<double>(secondLevel.missCnt)
                         / static_cast<double>(secondLevel.accessCnt);
        } else {
            l2MissRate = 0.0;
        }
    }
};
