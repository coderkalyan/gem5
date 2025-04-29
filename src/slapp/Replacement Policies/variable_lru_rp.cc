#include "mem/cache/replacement_policies/variable_lru_rp.hh"

#include <cassert>
#include <memory>

#include "params/VariableLRURP.hh"
#include "sim/cur_tick.hh"

namespace gem5 {

    namespace replacement_policy {

        VariableLRU::VariableLRU(const Params &p)
            : Base(p), scaleFactor(p.scaleFactor) {}

        void VariableLRU::invalidate(const std::shared_ptr<ReplacementData>& replacement_data) {
            // Reset last touch timestamp and chances
            std::static_pointer_cast<VariableLRUReplData>(replacement_data)->lastTouchTick = Tick(0);
            std::static_pointer_cast<VariableLRUReplData>(replacement_data)->chances = 0;
        }

        void VariableLRU::touch(const std::shared_ptr<ReplacementData>& replacement_data) const {
            // Update last touch timestamp
            std::static_pointer_cast<VariableLRUReplData>(replacement_data)->lastTouchTick = curTick();
        }

        void VariableLRU::reset(const std::shared_ptr<ReplacementData>& replacement_data) const {
            // Set last touch timestamp and chances
            std::static_pointer_cast<VariableLRUReplData>(replacement_data)->lastTouchTick = curTick();
            std::static_pointer_cast<VariableLRUReplData>(replacement_data)->chances = (scaleFactor == 0) ? 0 : (scaleFactor)
        }

        ReplaceableEntry* VariableLRU::getVictim(const ReplacementCandidates& candidates) const {
            // There must be at least one replacement candidate
            assert(candidates.size() > 0);

            // Visit all candidates to find victim
            ReplaceableEntry* victim = candidates[0];
            while (true) {
                // Find the candidate with the smallest lastTouchTick
                for (const auto& candidate : candidates) {
                    auto candidateData = std::static_pointer_cast<VariableLRUReplData>(candidate->replacementData);
                    auto victimData = std::static_pointer_cast<VariableLRUReplData>(victim->replacementData);
                    if (candidateData->lastTouchTick < victimData->lastTouchTick) {
                        victim = candidate;
                    }
                }

                // Check the victim's chances
                auto victimData = std::static_pointer_cast<VariableLRUReplData>(victim->replacementData);
                if (victimData->chances > 0) {
                    // Reset the tick and reduce the chances
                    victimData->lastTouchTick = curTick();
                    victimData->chances--;
                } else {
                    // Victim with no chances left found
                    break;
                }
            }

            // Return the victim entry
            return victim;
        }

        std::shared_ptr<ReplacementData> VariableLRU::instantiateEntry() {
            return std::make_shared<VariableLRUReplData>(new VariableLRUReplData());
        }
    }
}