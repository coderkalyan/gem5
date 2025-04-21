#include "amoeba/spatial_predictor.hh"

#include <algorithm>

namespace gem5 {

SpatialPatternPredictor::SpatialPatternPredictor(int size)
    : phtSize(size)
{}

void SpatialPatternPredictor::initialize(int num_subblocks, int word_size_bytes,
                                         int default_left, int default_right, 
                                         int prediction_mode)
{
    numSubblocks = num_subblocks;
    wordSize = word_size_bytes;
    defaultLeftWidth = default_left;
    defaultRightWidth = default_right;
    switch (prediction_mode) {
        case 0:
            basic_prediction = true;
            two_consecutive_misses = false;
            confidence_interval = false;
            break;
        case 1:
            basic_prediction = false;
            two_consecutive_misses = true;
            confidence_interval = false;
            break;
        case 2:
            basic_prediction = false;
            two_consecutive_misses = false;
            confidence_interval = true;
            break;
        default:
            basic_prediction = true;
            two_consecutive_misses = false;
            confidence_interval = false;
    }

    pht.clear();
    pht.resize(phtSize, makeEmptyPattern());
}

SpatialPatternPredictor::PHTEntry SpatialPatternPredictor::makeEmptyPattern() const {
    return PHTEntry{std::vector<bool>(numSubblocks, false), false};
}

int SpatialPatternPredictor::computeIndex(Addr address, Addr offset) const {
    return (address ^ (offset << 2)) % phtSize;
}

std::vector<bool> SpatialPatternPredictor::predictPattern(Addr address, int offset) {
    int index = computeIndex(address, offset);
    auto& entry = pht[index];
    if (entry.valid) {
        return entry.pattern;
    } else {
        return std::vector<bool>(numSubblocks, false);
    }
}

DirectionalPrediction SpatialPatternPredictor::predictWidth(Addr address, int offset, int subblock) {
    const std::vector<bool>& pattern = predictPattern(address, offset);

    // error checking
    if (pattern.empty() || pattern.size() != numSubblocks) {
        // pattern validity check
        return {defaultLeftWidth, defaultRightWidth};
    } else if (subblock < 0 || subblock >= numSubblocks) {
        // subblock validity check
        return {defaultLeftWidth, defaultRightWidth};
    } else if (std::all_of(pattern.begin(), pattern.end(), [](bool b){ return !b; })) {
        // all-zero pattern, no patter has yet been observed for this address
        return {defaultLeftWidth, defaultRightWidth};
    }

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
    } else if (two_consecutive_misses) {
        left_misses = 0;
        right_misses = 0;
        for (int i = subblock - 1; i >= 0; --i) {
            if (pattern[i]) {
                left++;
            } else {
                left_misses++;
                if (left_misses == 2)
                    break;
            }
        }
        for (int i = subblock + 1; i < numSubblocks; ++i) {
            if (pattern[i]) {
                right++;
            } else {
                right_misses++;
                if (right_misses == 2)
                    break;
            }
        }
    }

    return {left, right};
}

void SpatialPatternPredictor::updateCurrentPredictionTable(Addr address, int offset, int subblock) {
    uint64_t key = (static_cast<uint64_t>(address) << 12) | (offset & 0xFFF);
    auto& entry = cpt[key];

    if (entry.currentPattern.empty()) {
        entry.currentPattern.resize(numSubblocks, false);
    }

    if (subblock >= 0 && subblock < numSubblocks) {
        entry.currentPattern[subblock] = true;
    }

    entry.valid = true;
    entry.phtIndex = computeIndex(address, offset);
}

void SpatialPatternPredictor::updatePredictionHistoryTable(Addr address, int offset) {
    uint64_t key = (static_cast<uint64_t>(address) << 12) | (offset & 0xFFF);
    auto it = cpt.find(key);
    if (it != cpt.end() && it->second.valid) {
        int index = it->second.phtIndex;
        pht[index].pattern = it->second.currentPattern;
        pht[index].valid = true;
        cpt.erase(it);
    }
}

} // namespace gem5