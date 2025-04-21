#ifndef __AMOEBA_SPATIAL_PREDICTOR_HH__
#define __AMOEBA_SPATIAL_PREDICTOR_HH__

#include <vector>
#include <unordered_map>
#include <cstdint>

#include "base/types.hh"

namespace gem5 {

struct DirectionalPrediction {
    int leftWidth;
    int rightWidth;
};

class SpatialPatternPredictor
{
  public:
    explicit SpatialPatternPredictor(int phtSize);

    // Must be called before use
    void initialize(int num_subblocks, int word_size_bytes,
                    int default_left, int default_right);

    // Predict bit vector of expected subblock accesses
    std::vector<bool> predictPattern(Addr pc, int offset);

    // Predict left/right spatial range around subblock
    DirectionalPrediction predictWidth(Addr pc, int offset, int subblock);

    // Update subblock access in current CPT entry
    void updateCurrentPredictionTable(Addr pc, int offset, int subblock);

    // Commit CPT entry to global PHT
    void updatePredictionHistoryTable(Addr pc, int offset);

  private:
    struct PHTEntry {
        std::vector<bool> pattern;
        bool valid = false;
    };

    struct CPTEntry {
        std::vector<bool> currentPattern;
        int phtIndex;
        bool valid;
    };

    int computeIndex(Addr pc, Addr offset) const;
    PHTEntry makeEmptyPattern() const;

    int phtSize;
    int numSubblocks = 8;
    int wordSize = 8;

    int defaultLeftWidth = 0;
    int defaultRightWidth = 0;

    std::vector<PHTEntry> pht;
    std::unordered_map<uint64_t, CPTEntry> cpt;
};

} // namespace gem5

#endif // __AMOEBA_SPATIAL_PREDICTOR_HH__