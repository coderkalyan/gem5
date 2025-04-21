#include "amoeba/amoeba_cache.hh"

#include "debug/AmoebaCache.hh"

using namespace gem5;

// namespace gem5 {

AmoebaCache::AmoebaCache(const AmoebaCacheParams *params)
    : SimObject(*params), instPort(params->name + ".inst_port", this),
      dataPort(params->name + ".data_port", this),
      memPort(params->name + ".mem_port", this), blocked(false) {
}

Port &AmoebaCache::getPort(const std::string &if_name, PortID idx) {
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    if (if_name == "mem_side") {
        return memPort;
    } else if (if_name == "inst_port") {
        return instPort;
    } else if (if_name == "data_port") {
        return dataPort;
    } else {
        return SimObject::getPort(if_name, idx);
    }
}

AddrRangeList AmoebaCache::CPUSidePort::getAddrRanges() const {
    return owner->getAddrRanges();
}

void AmoebaCache::CPUSidePort::recvFunctional(PacketPtr pkt) {
    return owner->handleFunctional(pkt);
}

void AmoebaCache::handleFunctional(PacketPtr pkt) {
    memPort.sendFunctional(pkt);
}

AddrRangeList AmoebaCache::getAddrRanges() const {
    DPRINTF(AmoebaCache, "Sending new ranges\n");
    return memPort.getAddrRanges();
}

void AmoebaCache::MemSidePort::recvRangeChange() {
    owner->sendRangeChange();
}

void AmoebaCache::sendRangeChange() {
    instPort.sendRangeChange();
    dataPort.sendRangeChange();
}

bool AmoebaCache::CPUSidePort::recvTimingReq(PacketPtr pkt) {
    DPRINTF(AmoebaCache, "recv timing request\n");
    if (!owner->handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

bool AmoebaCache::handleRequest(PacketPtr pkt) {
    DPRINTF(AmoebaCache, "blocked: %d\n", blocked);
    if (blocked) {
        return false;
    }
    DPRINTF(AmoebaCache, "Got request for addr %#x\n", pkt->getAddr());
    blocked = true;
    memPort.sendPacket(pkt);
    return true;
}

void AmoebaCache::MemSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

void AmoebaCache::MemSidePort::recvReqRetry() {
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    sendPacket(pkt);
}

bool AmoebaCache::MemSidePort::recvTimingResp(PacketPtr pkt) {
    return owner->handleResponse(pkt);
}

bool AmoebaCache::handleResponse(PacketPtr pkt) {
    assert(blocked);
    DPRINTF(AmoebaCache, "Got response for addr %#x\n", pkt->getAddr());

    blocked = false;

    // Simply forward to the memory port
    if (pkt->req->isInstFetch()) {
        instPort.sendPacket(pkt);
    } else {
        dataPort.sendPacket(pkt);
    }

    instPort.trySendRetry();
    dataPort.trySendRetry();

    return true;
}

void AmoebaCache::CPUSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
    }
}

void AmoebaCache::CPUSidePort::recvRespRetry() {
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    sendPacket(pkt);
}

void AmoebaCache::CPUSidePort::trySendRetry() {
    if (needRetry && blockedPacket == nullptr) {
        needRetry = false;
        DPRINTF(AmoebaCache, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

AmoebaCache *AmoebaCacheParams::create() const {
    return new AmoebaCache(this);
}

// }; // namespace gem5
