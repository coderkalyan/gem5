/*
 * Copyright (c) 2017 Jason Lowe-Power
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <algorithm>
#include <cmath>

#include "base/compiler.hh"
#include "debug/SlappCache.hh"
#include "sim/system.hh"
#include "slapp/slapp_cache.hh"

namespace gem5 {

SlappCache::SlappCache(const SlappCacheParams &params)
    : ClockedObject(params), latency(params.latency), sets(params.sets),
      associativity(params.associativity), capacity(params.capacity),
      memPort(params.name + ".mem_side", this), blocked(false),
      originalPacket(nullptr), waitingPortId(-1), heap(capacity),
      writePointer(0), stats(this) {
  // Since the CPU side ports are a vector of ports, create an instance of
  // the CPUSidePort for each connection. This member of params is
  // automatically created depending on the name of the vector port and
  // holds the number of connections to this port name
  for (int i = 0; i < params.port_cpu_side_connection_count; ++i) {
    cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i), i, this);
  }

  // the size of the metadata array is a parameter of the cache
  // and is fixed at initialization time
  metadata.resize(sets);
  for (auto &set : metadata) {
    set.resize(associativity);
  }
}

Port &SlappCache::getPort(const std::string &if_name, PortID idx) {
  // This is the name from the Python SimObject declaration in SlappCache.py
  if (if_name == "mem_side") {
    panic_if(idx != InvalidPortID, "Mem side of slapp cache not a vector port");
    return memPort;
  } else if (if_name == "cpu_side" && idx < cpuPorts.size()) {
    // We should have already created all of the ports in the constructor
    return cpuPorts[idx];
  } else {
    // pass it along to our super class
    return ClockedObject::getPort(if_name, idx);
  }
}

void SlappCache::CPUSidePort::sendPacket(PacketPtr pkt) {
  // Note: This flow control is very simple since the cache is blocking.

  panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

  // If we can't send the packet across the port, store it for later.
  DPRINTF(SlappCache, "Sending %s to CPU\n", pkt->print());
  if (!sendTimingResp(pkt)) {
    DPRINTF(SlappCache, "failed!\n");
    blockedPacket = pkt;
  }
}

AddrRangeList SlappCache::CPUSidePort::getAddrRanges() const {
  return owner->getAddrRanges();
}

void SlappCache::CPUSidePort::trySendRetry() {
  if (needRetry && blockedPacket == nullptr) {
    // Only send a retry if the port is now completely free
    needRetry = false;
    DPRINTF(SlappCache, "Sending retry req.\n");
    sendRetryReq();
  }
}

void SlappCache::CPUSidePort::recvFunctional(PacketPtr pkt) {
  // Just forward to the cache.
  return owner->handleFunctional(pkt);
}

bool SlappCache::CPUSidePort::recvTimingReq(PacketPtr pkt) {
  DPRINTF(SlappCache, "Got request %s\n", pkt->print());

  if (blockedPacket || needRetry) {
    // The cache may not be able to send a reply if this is blocked
    DPRINTF(SlappCache, "Request blocked\n");
    needRetry = true;
    return false;
  }
  // Just forward to the cache.
  if (!owner->handleRequest(pkt, id)) {
    DPRINTF(SlappCache, "Request failed\n");
    // stalling
    needRetry = true;
    return false;
  } else {
    DPRINTF(SlappCache, "Request succeeded\n");
    return true;
  }
}

void SlappCache::CPUSidePort::recvRespRetry() {
  // We should have a blocked packet if this function is called.
  assert(blockedPacket != nullptr);

  // Grab the blocked packet.
  PacketPtr pkt = blockedPacket;
  blockedPacket = nullptr;

  DPRINTF(SlappCache, "Retrying response pkt %s\n", pkt->print());
  // Try to resend it. It's possible that it fails again.
  sendPacket(pkt);

  // We may now be able to accept new packets
  trySendRetry();
}

void SlappCache::MemSidePort::sendPacket(PacketPtr pkt) {
    if (blockedPacket != nullptr) {
        // Already have a blocked packet; need to wait until retry
        owner->schedule(new EventFunctionWrapper([this, pkt]() {
            this->sendPacket(pkt);
        }, owner->name() + ".retryMemSendEvent", true), owner->clockEdge(Cycles(1)));
        return;
    }

    if (!sendTimingReq(pkt)) {
        // Couldn't send immediately, need to wait for retry
        blockedPacket = pkt;
    }
}


bool SlappCache::MemSidePort::recvTimingResp(PacketPtr pkt) {
  // Just forward to the cache.
  return owner->handleResponse(pkt);
}

void SlappCache::MemSidePort::recvReqRetry() {
  // We should have a blocked packet if this function is called.
  assert(blockedPacket != nullptr);

  // Grab the blocked packet.
  PacketPtr pkt = blockedPacket;
  blockedPacket = nullptr;

  // Try to resend it. It's possible that it fails again.
  sendPacket(pkt);
}

void SlappCache::MemSidePort::recvRangeChange() { owner->sendRangeChange(); }

bool SlappCache::handleRequest(PacketPtr pkt, int port_id) {
  if (blocked) {
    // There is currently an outstanding request so we can't respond. Stall
    return false;
  }

  DPRINTF(SlappCache, "Got request for addr %#x\n", pkt->getAddr());
  if (pkt->getAddr() >= 512ULL * 1024 * 1024) { // Outside 512MB
    warn("SLAPP: Dropping insane address 0x%llx\n", pkt->getAddr());
    pkt->makeResponse();
    cpuPorts[port_id].sendPacket(pkt); 
    return true;
  }

  // This cache is now blocked waiting for the response to this packet.
  blocked = true;

  // Store the port for when we get the response
  assert(waitingPortId == -1);
  waitingPortId = port_id;

  // Schedule an event after cache access latency to actually access
  schedule(new EventFunctionWrapper([this, pkt] { accessTiming(pkt); },
                                    name() + ".accessEvent", true),
           clockEdge(latency));

  return true;
}

bool SlappCache::handleResponse(PacketPtr pkt) {
  assert(blocked);
  DPRINTF(SlappCache, "Got response for addr %#x\n", pkt->getAddr());
  DDUMP(SlappCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());

  // For now assume that inserts are off of the critical path and don't count
  // for any added latency.
  if (present.size() > 0) {
    // partial miss response, stitch together
    auto start = (uint64_t)(pkt->getAddr());
    auto end = (uint64_t)(pkt->getAddr() + pkt->getSize() - 1);
    for (auto it = present.begin(); it != present.end(); it++) {
      start = std::min(start, it->first);
      end = std::max(end, it->first);
    }

    std::vector<uint8_t> tmp(pkt->getSize());
    pkt->writeData(tmp.data());
    for (auto i = 0; i < pkt->getSize(); i++) {
      present[i + pkt->getAddr()] = tmp[i];
    }

    std::vector<uint8_t> buffer(end - start + 1);
    for (auto i = start; i <= end; i++) {
      buffer[i - start] = present.at(i);
    }

    const auto newPkt = new Packet(pkt, true, true);
    newPkt->setAddr(Addr(start));
    newPkt->setData(buffer.data());
    // pkt->setSize(buffer.size());
    insert(newPkt);
  } else {
    insert(pkt);
  }

  stats.missLatency.sample(curTick() - missTime);

  // If we had to upgrade the request packet to a full cache line, now we
  // can use that packet to construct the response.
  // if (originalPacket != nullptr) {
  //     DPRINTF(SlappCache, "Copying data from new packet to old\n");
  //     // We had to upgrade a previous packet. We can functionally deal with
  //     // the cache access now. It better be a hit.
  //     [[maybe_unused]] bool hit = accessFunctional(originalPacket);
  //     if (!hit) {
  //         const Addr packetAddr = originalPacket->getAddr();
  //         const uint64_t wordOffset = (packetAddr >> 3) & (rmax - 1);
  //         const uint64_t setIndex =
  //             ((uint64_t)packetAddr >> (uint64_t)std::log2(8 * rmax)) &
  //             (8 * rmax - 1);
  //         const uint64_t regionTag =
  //             ((uint64_t)packetAddr >> (uint64_t)std::log2(8 * rmax *
  //             sets)) & (8 * rmax * sets - 1);
  //
  //         DPRINTF(SlappCache,
  //                 "response: packetAddr = %lx, size = %lu, wordOffset =
  //                 %lx, " "setIndex = "
  //                 "%lx, regionTag "
  //                 "= %lx\n",
  //                 packetAddr, originalPacket->getSize(), wordOffset,
  //                 setIndex, regionTag);
  //
  //         auto &block = store[setIndex].back();
  //         DPRINTF(SlappCache,
  //                 "last block in set: setIndex = %lx, regionTag = %lx,
  //                 start "
  //                 "= %lx, end = "
  //                 "%lx\n",
  //                 setIndex, block.regionTag, block.start, block.end);
  //     }
  //
  //     panic_if(!hit, "Should always hit after inserting");
  //     originalPacket->makeResponse();
  //     delete pkt; // We may need to delay this, I'm not sure.
  //     pkt = originalPacket;
  //     originalPacket = nullptr;
  // } // else, pkt contains the data it needs

  sendResponse(pkt);

  return true;
}

void SlappCache::sendResponse(PacketPtr pkt) {
  assert(blocked);
  DPRINTF(SlappCache, "Sending resp for addr %#x\n", pkt->getAddr());
  DDUMP(SlappCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());

  int port = waitingPortId;

  // The packet is now done. We're about to put it in the port, no need for
  // this object to continue to stall.
  // We need to free the resource before sending the packet in case the CPU
  // tries to send another request immediately (e.g., in the same callchain).
  blocked = false;
  waitingPortId = -1;

  // Simply forward to the memory port
  cpuPorts[port].sendPacket(pkt);

  // For each of the cpu ports, if it needs to send a retry, it should do it
  // now since this memory object may be unblocked now.
  for (auto &port : cpuPorts) {
    port.trySendRetry();
  }
}

void SlappCache::handleFunctional(PacketPtr pkt) {
  if (accessFunctional(pkt)) {
    pkt->makeResponse();
  } else {
    memPort.sendFunctional(pkt);
  }
}

void SlappCache::accessTiming(PacketPtr pkt) {
  bool hit = accessFunctional(pkt);

  DPRINTF(SlappCache, "%s for packet: %s\n", hit ? "Hit" : "Miss",
          pkt->print());

  if (hit) {
    // Respond to the CPU side
    stats.hits++; // update stats
    DDUMP(SlappCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
    pkt->makeResponse();
    sendResponse(pkt);
  } else {
    stats.misses++; // update stats
    missTime = curTick();
    // Forward to the memory side.
    memPort.sendPacket(pkt);

    // FIXME: implement a spatial size predictor
    // Addr addr = pkt->getAddr();
    // auto blockSize = pkt->getSize();
    // Addr block_addr = pkt->getAddr();
    // unsigned size = pkt->getSize();
    // if (addr == block_addr && size == blockSize) {
    //     // Aligned and block size. We can just forward.
    //     DPRINTF(SlappCache, "forwarding packet\n");
    //     memPort.sendPacket(pkt);
    // } else {
    //     DPRINTF(SlappCache, "Upgrading packet to block size\n");
    //     panic_if(addr - block_addr + size > blockSize,
    //              "Cannot handle accesses that span multiple cache
    //              lines");
    //     // Unaligned access to one cache block
    //     assert(pkt->needsResponse());
    //     MemCmd cmd;
    //     if (pkt->isWrite() || pkt->isRead()) {
    //         // Read the data from memory to write into the block.
    //         // We'll write the data in the cache (i.e., a writeback
    //         cache) cmd = MemCmd::ReadReq;
    //     } else {
    //         panic("Unknown packet type in upgrade size");
    //     }
    //
    //     // Create a new packet that is blockSize
    //     PacketPtr new_pkt = new Packet(pkt->req, cmd, blockSize);
    //     new_pkt->allocate();
    //
    //     // Should now be block aligned
    //     assert(new_pkt->getAddr() == new_pkt->getBlockAddr(blockSize));
    //
    //     // Save the old packet
    //     originalPacket = pkt;
    //
    //     DPRINTF(SlappCache, "forwarding packet\n");
    //     memPort.sendPacket(new_pkt);
    // }
  }
}

bool SlappCache::accessFunctional(PacketPtr pkt) {
  const auto address = pkt->getAddr();
  const auto upper_bits = (uint64_t)(address) >> 6; // remove byte offset
  const auto set_index = upper_bits & (sets - 1);
  // const auto tag = upper_bits >> (uint64_t)(std::log2(sets));

  std::vector<std::pair<uint64_t, uint64_t>> evictions;
  uint64_t target = 0;
  for (const auto &meta : metadata[set_index]) {
    // check each of the tags in the set for a match
    // to simplify the code, we compare entire addresses,
    // but the hardware equivalent would do a tag and offset check
    const auto start = address;
    const auto end = address + pkt->getSize() - 1;
    const auto partial_miss =
        meta.valid && (((start <= meta.end) && (end > meta.end)) ||
                       ((start < meta.start) && (end >= meta.start)));
    if (partial_miss) {
      DPRINTF(SlappCache, "Partial miss: %x %x %x %x\n", start, end, meta.start,
              meta.end);
      // evict(set_index, target);
      evictions.emplace_back(set_index, target);
    }

    // panic_if(partial_miss, "Encountered partial miss unimplemented\n");

    target++;
    if (!(meta.valid && (start >= meta.start) && (end <= meta.end)))
      continue;

    // follow the indirection into the data heap
    const auto base = &heap.data()[meta.offset];
    const auto ptr = &base[start - meta.start];

    DPRINTF(SlappCache,
            "hit: set_index = %lx, start = %lx, size = %lx, offset = %lx, ptr "
            "= %p\n",
            set_index, address, pkt->getSize(),
            meta.offset + (start - meta.start), ptr);
    DDUMP(SlappCache, ptr, pkt->getSize());

    // hit, so potentially read or write from the packet
    if (pkt->isWrite()) {
      pkt->writeData(ptr);
    } else if (pkt->isRead()) {
      pkt->setData(ptr);
    } else {
      panic("Unknown packet type!");
    }

    // hit
    return true;
  }

  // miss, possibly partial
  present.clear();
  for (const auto &[set_index, target] : evictions) {
    const auto &meta = metadata[set_index][target];
    for (auto i = meta.start; i <= meta.end; i++) {
      present[i] = heap.data()[meta.offset + i - meta.start];
    }

    evict(set_index, target, false);
  }

  return false;
}

void SlappCache::evict(const uint64_t set_index, const uint64_t target,
                       bool write_back) {
  panic_if(target >= associativity,
           "Should never evict a block that doesn't exist");

  // write back the data
  // create a new request-packet pair
  auto &meta = metadata[set_index][target];
  const auto offset = meta.offset;
  const auto size = meta.end - meta.start + 1;
  RequestPtr req = std::make_shared<Request>(meta.start, size, 0, 0);
  PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, size);
  new_pkt->dataStatic(&heap.data()[offset]);

  DPRINTF(SlappCache, "Writing packet back %s\n", new_pkt->print());
  if (write_back)
    memPort.sendPacket(new_pkt);
  auto write_ptr = offset, read_ptr = offset + size;
  while (read_ptr < writePointer) {
    heap[write_ptr++] = heap[read_ptr++];
  }

  for (auto &set : metadata) {
    for (auto &meta : set) {
      if (meta.offset >= offset) {
        meta.offset -= size;
      }
    }
  }

  writePointer -= size;
  DPRINTF(SlappCache, "write Pointer: %x %x\n", writePointer, write_ptr);
  panic_if(writePointer != write_ptr,
           "Write pointers should sync after eviction");
  meta.valid = false;
  meta.start = 0;
  meta.end = 0;
  meta.offset = 0;
}

void SlappCache::insert(PacketPtr pkt) {
  // The address should not be in the cache
  assert(!accessFunctional(pkt));
  // The pkt should be a response
  assert(pkt->isResponse());

  const auto address = pkt->getAddr();
  const auto upper_bits = (uint64_t)(address) >> 6; // remove byte offset
  const auto set_index = upper_bits & (sets - 1);
  // const auto tag = upper_bits >> (uint64_t)(std::log2(sets));

  // FIXME: implement eviction policy, currently round robin
  static auto victim = 0;
  auto target = victim;
  victim = (victim + 1) % associativity;
  for (size_t i = 0; i < associativity; i++) {
    if (metadata[set_index][i].valid)
      continue;
    target = i;
    break;
  }

  panic_if(target >= associativity,
           "Should never evict a block that doesn't exist");

  if (metadata[set_index][target].valid) {
    evict(set_index, target, true);
  }

  panic_if((writePointer + pkt->getSize()) > capacity,
           "Not enough space in the cache to insert the block");

  DPRINTF(SlappCache, "Inserting %s\n", pkt->print());
  DDUMP(SlappCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());

  // Insert the data and address into the cache store
  DPRINTF(SlappCache, "Inserting: set_index = %lx, start = %lx, size = %lx\n",
          set_index, address, pkt->getSize());

  // update the metadata array
  const auto start = address;
  const auto end = address + pkt->getSize() - 1;
  metadata[set_index][target].valid = true;
  metadata[set_index][target].start = start;
  metadata[set_index][target].end = end;
  metadata[set_index][target].offset = writePointer;
  // write the new data into the cache
  const auto ptr = &heap.data()[writePointer];
  pkt->writeData(ptr);
  DDUMP(SlappCache, ptr, pkt->getSize());
  writePointer += pkt->getSize();

  DDUMP(SlappCache, &heap.data()[metadata[set_index][target].offset],
        pkt->getSize());

  DPRINTF(SlappCache, "offset: %lx, ptr: %p\n",
          metadata[set_index][target].offset, ptr);
}

AddrRangeList SlappCache::getAddrRanges() const {
  DPRINTF(SlappCache, "Sending new ranges\n");
  // Just use the same ranges as whatever is on the memory side.
  return memPort.getAddrRanges();
}

void SlappCache::sendRangeChange() const {
  for (auto &port : cpuPorts) {
    port.sendRangeChange();
  }
}

SlappCache::SlappCacheStats::SlappCacheStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(hits, statistics::units::Count::get(), "Number of hits"),
      ADD_STAT(misses, statistics::units::Count::get(), "Number of misses"),
      ADD_STAT(missLatency, statistics::units::Tick::get(),
               "Ticks for misses to the cache"),
      ADD_STAT(hitRatio, statistics::units::Ratio::get(),
               "The ratio of hits to the total accesses to the cache",
               hits / (hits + misses)) {
  missLatency.init(16); // number of buckets
}

} // namespace gem5
