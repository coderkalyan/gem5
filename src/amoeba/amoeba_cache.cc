#include "amoeba/amoeba_cache.hh"
#include "sim/system.hh"

#include "debug/AmoebaCache.hh"

using namespace gem5;

// namespace gem5 {

AmoebaCache::AmoebaCache(const AmoebaCacheParams &params)
    : ClockedObject(params), memPort(params.name + ".mem_port", this),
      blocked(false), latency(params.latency),
      blockSize(params.system->cacheLineSize()),
      capacity(params.size / blockSize) {
    for (int i = 0; i < params.port_cpu_side_connection_count; i++) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i), i, this);
    }
}

Port &AmoebaCache::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "mem_side") {
        panic_if(idx != InvalidPortID,
                 "Mem side of simple cache not a vector port");
        return memPort;
    } else if (if_name == "cpu_side" && idx < cpuPorts.size()) {
        return cpuPorts[idx];
    } else {
        return ClockedObject::getPort(if_name, idx);
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
    for (auto &port : cpuPorts) {
        port.sendRangeChange();
    }
}

bool AmoebaCache::CPUSidePort::recvTimingReq(PacketPtr pkt) {
    DPRINTF(AmoebaCache, "recv timing request\n");
    if (!owner->handleRequest(pkt, id)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

bool AmoebaCache::handleRequest(PacketPtr pkt, int portId) {
    if (blocked) {
        return false;
    }
    DPRINTF(AmoebaCache, "Got request for addr %#x\n", pkt->getAddr());
    blocked = true;
    waitingPortId = portId;
    schedule(new AccessEvent(this, pkt), clockEdge(latency));
    return true;
}

void AmoebaCache::accessTiming(PacketPtr pkt) {
    bool hit = accessFunctional(pkt);
    if (hit) {
        pkt->makeResponse();
        sendResponse(pkt);
    } else {
        Addr addr = pkt->getAddr();
        Addr block_addr = pkt->getBlockAddr(blockSize);
        unsigned size = pkt->getSize();
        if (addr == block_addr && size == blockSize) {
            DPRINTF(AmoebaCache, "forwarding packet\n");
            memPort.sendPacket(pkt);
        } else {
            DPRINTF(AmoebaCache, "Upgrading packet to block size\n");
            panic_if(addr - block_addr + size > blockSize,
                     "Cannot handle accesses that span multiple cache lines");

            assert(pkt->needsResponse());
            MemCmd cmd;
            if (pkt->isWrite() || pkt->isRead()) {
                cmd = MemCmd::ReadReq;
            } else {
                panic("Unknown packet type in upgrade size");
            }

            PacketPtr new_pkt = new Packet(pkt->req, cmd, blockSize);
            new_pkt->allocate();

            outstandingPacket = pkt;

            memPort.sendPacket(new_pkt);
        }
    }
}

void AmoebaCache::sendResponse(PacketPtr pkt) {
    int port = waitingPortId;

    blocked = false;
    waitingPortId = -1;

    cpuPorts[port].sendPacket(pkt);
    for (auto &port : cpuPorts) {
        port.trySendRetry();
    }
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
    insert(pkt);

    blocked = false;

    if (outstandingPacket != nullptr) {
        accessFunctional(outstandingPacket);
        outstandingPacket->makeResponse();
        delete pkt;
        pkt = outstandingPacket;
        outstandingPacket = nullptr;
    }

    sendResponse(pkt);
    return true;
}

bool AmoebaCache::accessFunctional(PacketPtr pkt) {
    Addr block_addr = pkt->getBlockAddr(blockSize);
    auto it = cacheStore.find(block_addr);
    if (it != cacheStore.end()) {
        if (pkt->isWrite()) {
            pkt->writeDataToBlock(it->second, blockSize);
        } else if (pkt->isRead()) {
            pkt->setDataFromBlock(it->second, blockSize);
        } else {
            panic("Unknown packet type!");
        }
        return true;
    }
    return false;
}

void AmoebaCache::insert(PacketPtr pkt) {
    if (cacheStore.size() >= capacity) {
        // Select random thing to evict. This is a little convoluted since we
        // are using a std::unordered_map. See http://bit.ly/2hrnLP2
        int bucket, bucket_size;
        do {
            bucket = rng->random(0, (int)cacheStore.bucket_count() - 1);
        } while ((bucket_size = cacheStore.bucket_size(bucket)) == 0);
        auto block = std::next(cacheStore.begin(bucket),
                               rng->random(0, bucket_size - 1));

        RequestPtr req =
            std::make_shared<Request>(block->first, blockSize, 0, 0);
        PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
        new_pkt->dataDynamic(block->second); // This will be deleted later

        DPRINTF(AmoebaCache, "Writing packet back %s\n", pkt->print());
        memPort.sendTimingReq(new_pkt);

        cacheStore.erase(block->first);
    }
    uint8_t *data = new uint8_t[blockSize];
    cacheStore[pkt->getAddr()] = data;

    pkt->writeDataToBlock(data, blockSize);
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

// }; // namespace gem5
