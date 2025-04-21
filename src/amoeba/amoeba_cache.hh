#pragma once

#include "mem/port.hh"
#include "params/AmoebaCache.hh"
#include "sim/sim_object.hh"

using namespace gem5;
// namespace gem5 {
class AmoebaCache : public SimObject
{
  private:
    class CPUSidePort : public ResponsePort
    {
      private:
        AmoebaCache *owner;
        bool needRetry;
        PacketPtr blockedPacket;

      public:
        CPUSidePort(const std::string &name, AmoebaCache *owner)
            : ResponsePort(name), owner(owner), needRetry(false),
              blockedPacket(nullptr) {}
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

    class MemSidePort : public RequestPort
    {
      private:
        AmoebaCache *owner;
        PacketPtr blockedPacket;

      public:
        MemSidePort(const std::string &name, AmoebaCache *owner)
            : RequestPort(name), owner(owner), blockedPacket(nullptr) {}
        void sendPacket(PacketPtr pkt);

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    CPUSidePort instPort;
    CPUSidePort dataPort;
    MemSidePort memPort;
    bool blocked;

  public:
    AmoebaCache(const AmoebaCacheParams *params);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    void handleFunctional(PacketPtr pkt);
    AddrRangeList getAddrRanges() const;
    void sendRangeChange();
    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
};
// }; // namespace gem5
