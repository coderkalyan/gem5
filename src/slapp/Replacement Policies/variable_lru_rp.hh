/**
 * @file
 * Declaration of a Variable LRU replacement policy.
 * The victim is chosen using the last touch timestamp, with the number
 * of chances determined by its block size. 
 *
 * Our implementation has a variable chance assuming linear scaling of
 * the block size, with a scale factor provided by the user. A 0 scale
 * would indicate a traditional LRU policy.
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_VARIABLE_LRU_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_VARIABLE_LRU_RP_HH__

#include "mem/cache/replacement_policies/base.hh"

namespace gem5 {

struct VariableLRURPParams;

namespace replacement_policy {
    
    class VariableLRU : public Base{
        protected:
        const signed scaleFactor; // Scale factor for the block size
        struct VariableLRUReplData : ReplacementData
        {
            Tick lastTouchTick; // Last touch tick
            unsigned chances; // Number of chances for this entry

            VariableLRUReplData() : lastTouchTick(0), chances(0) {}
        };


        public:
        typedef VariableLRURPParams Params;
        VariableLRU(const Params &p);
        ~VariableLRU() = default;

        /**
         * Invalidate replacement data to set it as the next probable victim.
         * Sets its last touch tick as the starting tick, and the chances to 0.
         *
         * @param replacement_data Replacement data to be invalidated.
         */
        void invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
        override;

        /**
        * Touch an entry to update its replacement data.
        * Sets its last touch tick as the current tick, do not change chances.
        *
        * @param replacement_data Replacement data to be touched.
        */
        void touch(const std::shared_ptr<ReplacementData>& replacement_data) const
            override;

        /**
        * Reset replacement data. Used when an entry is inserted.
        * Sets its last touch tick as the current tick and the chances to predefined
        * value.
        *
        * @param replacement_data Replacement data to be reset.
        */
        void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
            override;

        /**
        * Find replacement victim using LRU timestamps. Select the entry with the
        * lowest timestamp that has no chances left.
        *
        * @param candidates Replacement candidates, selected by indexing policy.
        * @return Replacement entry to be replaced.
        */
        ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const
            override;

        /**
        * Instantiate a replacement data entry.
        *
        * @return A shared pointer to the new replacement data.
        */
        std::shared_ptr<ReplacementData> instantiateEntry() override;
    };
} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_VARIABLE_LRU_RP_HH__