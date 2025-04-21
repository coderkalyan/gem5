#pragma once

#include "base/random.hh"
#include "mem/port.hh"
#include "params/AmoebaCache.hh"
#include "sim/clocked_object.hh"
#include "sim/sim_object.hh"

using namespace gem5;
// namespace gem5 {
class AmoebaCache : public ClockedObject {
  private:
    class CPUSidePort : public ResponsePort {
      private:
        int id;
        AmoebaCache *owner;
        bool needRetry;
        PacketPtr blockedPacket;

      public:
        CPUSidePort(const std::string &name, int id, AmoebaCache *owner)
            : ResponsePort(name), id(id), owner(owner), needRetry(false),
              blockedPacket(nullptr) {
        }
        void sendPacket(PacketPtr pkt);
        void trySendRetry();

        AddrRangeList getAddrRanges() const override;

      protected:
        Tick recvAtomic(PacketPtr pkt) override {
            panic("recvAtomic unimplemented.");
        }

        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    class MemSidePort : public RequestPort {
      private:
        AmoebaCache *owner;
        PacketPtr blockedPacket;

      public:
        MemSidePort(const std::string &name, AmoebaCache *owner)
            : RequestPort(name), owner(owner), blockedPacket(nullptr) {
        }
        void sendPacket(PacketPtr pkt);

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    class AccessEvent : public Event {
      private:
        AmoebaCache *cache;
        PacketPtr pkt;

      public:
        AccessEvent(AmoebaCache *cache, PacketPtr pkt)
            : Event(Default_Pri, AutoDelete), cache(cache), pkt(pkt) {
        }
        void process() override {
            cache->accessTiming(pkt);
        }
    };

    std::vector<CPUSidePort> cpuPorts;
    MemSidePort memPort;
    bool blocked;
    Cycles latency;
    int blockSize;
    int capacity;
    int waitingPortId;
    PacketPtr outstandingPacket;
    std::unordered_map<Addr, uint8_t *> cacheStore;
    Random::RandomPtr rng = Random::genRandom();

  public:
    AmoebaCache(const AmoebaCacheParams &params);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    void handleFunctional(PacketPtr pkt);
    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    bool handleRequest(PacketPtr pkt, int portId);
    bool handleResponse(PacketPtr pkt);
    void accessTiming(PacketPtr pkt);
    void sendResponse(PacketPtr pkt);
    bool accessFunctional(PacketPtr pkt);
    void insert(PacketPtr pkt);
};
// }; // namespace gem5
