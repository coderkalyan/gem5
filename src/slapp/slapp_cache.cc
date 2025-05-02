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
#include <cstdio>

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

AddrRangeList SlappCache::CPUSidePort::getAddrRanges() const {
  return owner->getAddrRanges();
}

// Receive a request packet from the CPU and try to process it.
// If the cache is currently busy (waiting for memory to respond)
// the request is rejected.
bool SlappCache::CPUSidePort::recvTimingReq(PacketPtr pkt) {
  DPRINTF(SlappCache, "Begin transaction --------\n");
  DPRINTF(SlappCache, "Received CPU timing request: %s\n", pkt->print());

  // currently we only support word sized requests
  if (pkt->getSize() > 8) {
    DPRINTF(SlappCache, "Surprisingly large packet: %d bytes\n",
            pkt->getSize());
    panic("unexpected packet size");
  }

  // this is a blocking cache, so if the CPU sends another request
  // before it has accepted a previous response, reject it
  // only one request in flight at a time
  if (outstanding.has_value()) {
    DPRINTF(SlappCache, "Timing request %s blocked on response %s\n",
            pkt->print(), outstanding.value()->print());
    retry = true; // remember to send a retry request once unblocked
    return false;
  }

  // forward the request to the cache, which may reject it
  // if blocked on a memory request.
  const auto handled = owner->handleTimingReq(pkt, id);
  if (handled) {
    DPRINTF(SlappCache, "Timing request %s succeeded\n", pkt->print());
    return true;
  } else {
    DPRINTF(SlappCache, "Timing request %s blocked on cache busy\n",
            pkt->print());
    retry = true; // remember to send a retry request once unblocked
    return false;
  }
}

void SlappCache::CPUSidePort::recvFunctional(PacketPtr pkt) {
  // Functional requests cannot fail, so blindly forward
  // to the cache.
  owner->handleFunctional(pkt);
}

void SlappCache::CPUSidePort::sendPacket(PacketPtr pkt) {
  // reply to the original cpu request with a response packet.
  panic_if(outstanding.has_value(), "Should never try to send if blocked!");

  // If the CPU is busy, store the oustanding packet and retry later.
  DPRINTF(SlappCache, "Sending response %s\n", pkt->print());
  const auto received = sendTimingResp(pkt);
  if (received) {
    DPRINTF(SlappCache, "CPU accepted cache response for %s\n", pkt->print());
  } else {
    DPRINTF(SlappCache, "CPU response port busy for %s\n", pkt->print());
    outstanding = pkt;
  }
}

void SlappCache::CPUSidePort::recvRespRetry() {
  // Called by the CPU if it previously failed to accept a response
  // and is now (potentially) available.

  // We should have a blocked packet if this function is called.
  panic_if(!outstanding.has_value(), "Received spurious response retry\n");

  const auto pkt = outstanding.value();
  DPRINTF(SlappCache, "Retrying response %s\n", pkt->print());
  sendPacket(pkt);

  // if the response port is now free (we successfully sent the
  // packet above, AND the CPU was waiting on a request, tell
  // it to retry the request
  trySendRetry();
}

void SlappCache::CPUSidePort::trySendRetry() {
  if (!outstanding.has_value() && retry) {
    DPRINTF(SlappCache,
            "Response succeeded and CPU waiting, sending retry request.\n");
    retry = false;
    sendRetryReq();
  }
}

void SlappCache::MemSidePort::recvRangeChange() { owner->sendRangeChange(); }

bool SlappCache::MemSidePort::process() {
  if (outstanding.empty()) {
    DPRINTF(SlappCache, "Finished processing memory queue\n");
    return true;
  }

  // Grab the first packet (we process in order) and try to send it.
  const auto &pkt = outstanding.front();
  const auto sent = sendTimingReq(pkt);
  if (sent) {
    DPRINTF(SlappCache, "Successfully sent pkt %x %s from queue\n", pkt,
            pkt->print());
    outstanding.pop();
  } else {
    DPRINTF(SlappCache, "Memory request port busy\n");
  }

  return outstanding.empty();
}

bool SlappCache::MemSidePort::recvTimingResp(PacketPtr pkt) {
  DPRINTF(SlappCache, "Received memory timing response for %s\n", pkt->print());
  // In case of a partial miss, the first packets in the queue
  // are all write packets, and the final packet is a read.
  if (outstanding.empty()) {
    // Forward this read response to the owner
    panic_if(!pkt->isRead(), "Expected last memory response to be read");
    DPRINTF(SlappCache,
            "Received response to read request %s, forwarding to cache\n",
            pkt->print());
    return owner->handleTimingResp(pkt);
  } else {
    panic_if(!pkt->isWrite(), "Expected memory response to be write");
    DPRINTF(SlappCache, "Received response to write request %s. Sending next\n",
            pkt->print());
    // FIXME: is it correct to send another packet or
    // do we need to acknowledge first?
    owner->schedule(new EventFunctionWrapper([this] { process(); },
                                             name() + ".processEvent", true),
                    owner->clockEdge(Cycles(1)));
    return true;
    // return process();
  }
}

void SlappCache::MemSidePort::recvReqRetry() {
  // Memory is ready for us to try to send something from the queue.

  // We should not have popped a failed packet from the queue.
  panic_if(outstanding.empty(), "Expected outstanding packet(s)");

  process();
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

bool SlappCache::handleTimingReq(PacketPtr pkt, int port_id) {
  if (blocked) {
    // There is currently an outstanding request so we can't respond.
    return false;
  }

  // This cache is now blocked waiting for the response to this packet.
  blocked = true;
  DPRINTF(SlappCache, "Handling request pkt %s\n", pkt->print());

  // Store the port for when we get the response
  assert(waitingPortId == -1);
  waitingPortId = port_id;

  // Schedule an event after cache access latency to actually access
  // accessTiming(pkt);
  schedule(new EventFunctionWrapper([this, pkt] { accessTiming(pkt); },
                                    name() + ".accessEvent", true),
           clockEdge(latency));

  return true;
}

bool SlappCache::handleTimingResp(PacketPtr pkt) {
  // Should not get here unless blocked (waiting for read from memory)
  assert(blocked);

  DPRINTF(SlappCache, "Received read response pkt %s\n", pkt->print());
  DDUMP(SlappCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());

  // For now assume that inserts are off of the critical path and don't count
  // for any added latency.
  // Because we currently flush all partial miss packets and re-request
  // the entire (prefetched) range, there is no need to assemble anything
  // here. Just insert the received packet into cache.
  insert(pkt);

  // Update timing statistics based on how long it took to fetch data
  // from memory, including potential flush.
  stats.missLatency.sample(curTick() - missTime);

  // If we upgraded the request packet to a larger block size than
  // the original request packet, we need to construct the response.
  // Otherwise just use the response packet.
  if (originalPacket != nullptr) {
    DPRINTF(SlappCache, "Assembling response pkt %s from block read %s\n",
            originalPacket->print(), pkt->print());

    // We had to upgrade a previous packet. We can functionally deal with
    // the cache access now. It better be a hit since we inserted.
    DPRINTF(SlappCache, "Original packet: %s\n", originalPacket->print());
    const bool hit = accessFunctional(originalPacket);
    if (!hit) {
      DPRINTF(SlappCache,
              "Expected hit after inserting packet: inserted %s original %s\n",
              pkt->print(), originalPacket->print());
      panic("Expected hit after inserting packet");
    }

    DPRINTF(SlappCache, "Making response: %x %s\n", originalPacket,
            originalPacket->print());
    originalPacket->makeResponse();
    // delete pkt;
    pkt = originalPacket;
    originalPacket = nullptr;
  }

  // Send the response to the CPU now. We never fail or block here.
  sendResponse(pkt);
  return true;
}

void SlappCache::sendResponse(PacketPtr pkt) {
  // We should still be blocked when we respond to the CPU.
  assert(blocked);

  DPRINTF(SlappCache, "Sending response for pkt %s\n", pkt->print());
  DDUMP(SlappCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());

  const auto port = waitingPortId;

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
  // DPRINTF(SlappCache, "Handling functional request for %s\n", pkt->print());
  const auto hit = accessFunctional(pkt);
  if (hit) {
    assert(memPort.outstanding.empty());
    pkt->makeResponse();
  } else {
    while (!memPort.outstanding.empty()) {
      memPort.sendFunctional(memPort.outstanding.front());
      memPort.outstanding.pop();
    }
    memPort.sendFunctional(pkt);
  }
}

void SlappCache::accessTiming(PacketPtr pkt) {
  assert(memPort.outstanding.empty());
  const auto hit = accessFunctional(pkt);
  if (hit)
    assert(memPort.outstanding.empty());

  DPRINTF(SlappCache, "%s for packet: %s\n", hit ? "Hit" : "Miss",
          pkt->print());

  if (hit) {
    // Respond to the CPU side
    stats.hits++; // update stats
    DDUMP(SlappCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
    pkt->makeResponse();
    assert(originalPacket == nullptr);
    sendResponse(pkt);
  } else {
    stats.misses++; // update stats
    missTime = curTick();

    // Pre-allocate a slot in the metadata array if we need to evict.
    // This ensures we process all eviction transactions strictly
    // before the read request that fills the cache.
    allocate(pkt);

    // Forward to the memory side. This needs to be upgraded
    // to the prefetch size.

    auto address = pkt->getAddr();
    auto size = pkt->getSize();
    assert(size <= 8);

    // FIXME: implement prefetching
    size += address - (address & ~7);
    size = ((size + 7) / 8) * 8;
    address = address & ~7;
    DPRINTF(SlappCache, "Miss for packet %s, upgrading to address %x size %d\n",
            pkt->print(), address, size);

    // Unaligned access to one cache block
    assert(pkt->needsResponse());
    MemCmd cmd;
    if (pkt->isWrite() || pkt->isRead()) {
      // Read the data from memory to write into the block.
      // We'll write the data in the cache (i.e., a writeback
      // cache)
      cmd = MemCmd::ReadReq;
    } else {
      panic("Unknown packet type in upgrade size");
    }

    // Create a new packet that is blockSize
    PacketPtr new_pkt = new Packet(pkt->req, cmd, size);
    new_pkt->setAddr(address);
    new_pkt->allocate();

    // Should now be block aligned
    DPRINTF(SlappCache, "Aligned address: %x %x\n", new_pkt->getAddr(),
            address);
    assert(new_pkt->getAddr() == address);

    // Save the old packet
    originalPacket = pkt;

    DPRINTF(SlappCache, "Requesting pkt %s from memory\n", new_pkt->print());
    memPort.outstanding.push(new_pkt);

    // start processing the queue of zero or more writebacks and one read
    memPort.process();
  }
}

bool SlappCache::accessFunctional(PacketPtr pkt) {
  const auto address = pkt->getAddr();
  const auto upper_bits = (uint64_t)(address) >> 6; // remove byte offset
  const auto set_index = upper_bits & (sets - 1);
  // const auto tag = upper_bits >> (uint64_t)(std::log2(sets));

  // assert(memPort.outstanding.empty());

  for (uint64_t target = 0; target < associativity; target++) {
    // check each of the tags in the set for a match
    // to simplify the code, we compare entire addresses,
    // but the hardware equivalent would do a tag and offset check
    const auto &meta = metadata[set_index][target];
    const auto start = address;
    const auto end = address + pkt->getSize() - 1;
    const auto partial_miss =
        meta.valid && (((start <= meta.end) && (end > meta.end)) ||
                       ((start < meta.start) && (end >= meta.start)));
    if (partial_miss) {
      DPRINTF(SlappCache, "Partial miss: %x %x %x %x\n", start, end, meta.start,
              meta.end);
      evict(set_index, target);
    }

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
    // we should not have any partial misses in this case
    assert(memPort.outstanding.empty());
    return true;
  }

  // miss, possibly partial
  if (!memPort.outstanding.empty()) {
    const auto &pkt = memPort.outstanding.front();
    DPRINTF(SlappCache, "Miss: outstanding %x %s\n", pkt, pkt->print());
  }
  return false;
}

void SlappCache::evict(const uint64_t set_index, const uint64_t target) {
  panic_if(target >= associativity,
           "Should never evict a block that doesn't exist");

  // queue the data for writeback
  // create a new request-packet pair
  auto &meta = metadata[set_index][target];
  const auto offset = meta.offset;
  const auto size = meta.end - meta.start + 1;
  RequestPtr req = std::make_shared<Request>(meta.start, size, 0, 0);
  // PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, size);
  PacketPtr new_pkt = new Packet(req, MemCmd::WriteReq, size);
  uint8_t *const data = new uint8_t[size];
  std::copy(&heap.data()[offset], &heap.data()[offset + size - 1], data);
  new_pkt->dataDynamic(data);

  DPRINTF(SlappCache, "Writing packet back %s\n", new_pkt->print());
  DDUMP(SlappCache, new_pkt->getConstPtr<uint8_t>(), new_pkt->getSize());

  // Queue the write to memory.
  memPort.outstanding.push(new_pkt);

  // eagerly defragment the heap
  auto write_ptr = offset, read_ptr = offset + size;
  while (read_ptr < writePointer) {
    heap[write_ptr++] = heap[read_ptr++];
  }

  // adjust any stale pointers in the metadata array
  for (auto &set : metadata) {
    for (auto &meta : set) {
      if (meta.offset >= offset) {
        meta.offset -= size;
      }
    }
  }

  writePointer -= size;
  // DPRINTF(SlappCache, "write Pointer: %x %x\n", writePointer, write_ptr);
  panic_if(writePointer != write_ptr,
           "Write pointers should sync after eviction");

  // clear the current way
  meta.valid = false;
  meta.start = 0;
  meta.end = 0;
  meta.offset = 0;
}

void SlappCache::allocate(PacketPtr pkt) {
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
    evict(set_index, target);
  }

  // process the eviction if needed
  // memPort.process();
}

void SlappCache::insert(PacketPtr pkt) {
  // The address should not be in the cache, we don't
  // want synonyms
  assert(!accessFunctional(pkt));
  // this false access functional could cause unecessary evictions,
  // so clear the queue

  // The pkt should be a response
  assert(pkt->isResponse());

  const auto address = pkt->getAddr();
  const auto upper_bits = (uint64_t)(address) >> 6; // remove byte offset
  const auto set_index = upper_bits & (sets - 1);
  // const auto tag = upper_bits >> (uint64_t)(std::log2(sets));

  // FIXME: implement eviction policy, currently round robin
  auto target = 0;
  for (size_t i = 0; i < associativity; i++) {
    if (metadata[set_index][i].valid)
      continue;
    target = i;
    break;
  }

  panic_if(target >= associativity,
           "Should never evict a block that doesn't exist");

  // we should have made space earlier in allocate()
  panic_if(metadata[set_index][target].valid, "Expected empty space");

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
