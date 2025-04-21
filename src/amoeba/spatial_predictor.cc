#include "amoeba/spatial_predictor.hh"

#include <algorithm>

namespace gem5 {

SpatialPatternPredictor::SpatialPatternPredictor(int size)
    : phtSize(size)
{}

void SpatialPatternPredictor::initialize(int num_subblocks, int word_size_bytes,
                                         int default_left, int default_right)
{
    numSubblocks = num_subblocks;
    wordSize = word_size_bytes;
    defaultLeftWidth = default_left;
    defaultRightWidth = default_right;

    pht.clear();
    pht.resize(phtSize, makeEmptyPattern());
}

SpatialPatternPredictor::PHTEntry SpatialPatternPredictor::makeEmptyPattern() const {
    return PHTEntry{std::vector<bool>(numSubblocks, false), false};
}

int SpatialPatternPredictor::computeIndex(Addr pc, Addr offset) const {
    return (pc ^ (offset << 2)) % phtSize;
}

std::vector<bool> SpatialPatternPredictor::predictPattern(Addr pc, int offset) {
    int index = computeIndex(pc, offset);
    auto& entry = pht[index];
    if (entry.valid) {
        return entry.pattern;
    } else {
        return std::vector<bool>(numSubblocks, false);
    }
}

DirectionalPrediction SpatialPatternPredictor::predictWidth(Addr pc, int offset, int subblock) {
    const std::vector<bool>& pattern = predictPattern(pc, offset);

    // error checking
    if (pattern.empty() || pattern.size() != numSubblocks) {
        // pattern validity check
        return {defaultLeftWidth, defaultRightWidth};
    } else if (subblock < 0 || subblock >= numSubblocks) {
        // subblock validity check
        return {defaultLeftWidth, defaultRightWidth};
    } else if (std::all_of(pattern.begin(), pattern.end(), [](bool b){ return !b; })) {
        // all-zero pattern, no patter has yet been observed for this pc
        return {defaultLeftWidth, defaultRightWidth};
    }

    // basic left-right prediction; we can also use a confidence interval or require two consecutive misses to break
    basic_prediction = true;
    two_consecutive_misses = false;
    confidence_interval = false;

    int left = 0;
    int right = 0;

    if (basic_prediction) {
        for (int i = subblock - 1; i >= 0; --i) {
            if (pattern[i])
                left++;
            else
                break;
        }
        for (int i = subblock + 1; i < numSubblocks; ++i) {
            if (pattern[i])
                right++;
            else
                break;
        }
    }

    return {left, right};
}

void SpatialPatternPredictor::updateCurrentPredictionTable(Addr pc, int offset, int subblock) {
    uint64_t key = (static_cast<uint64_t>(pc) << 12) | (offset & 0xFFF);
    auto& entry = cpt[key];

    if (entry.currentPattern.empty()) {
        entry.currentPattern.resize(numSubblocks, false);
    }

    if (subblock >= 0 && subblock < numSubblocks) {
        entry.currentPattern[subblock] = true;
    }

    entry.valid = true;
    entry.phtIndex = computeIndex(pc, offset);
}

void SpatialPatternPredictor::updatePredictionHistoryTable(Addr pc, int offset) {
    uint64_t key = (static_cast<uint64_t>(pc) << 12) | (offset & 0xFFF);
    auto it = cpt.find(key);
    if (it != cpt.end() && it->second.valid) {
        int index = it->second.phtIndex;
        pht[index].pattern = it->second.currentPattern;
        pht[index].valid = true;
        cpt.erase(it);
    }
}

} // namespace gem5