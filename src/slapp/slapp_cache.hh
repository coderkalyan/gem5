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

#pragma once

#include <queue>
#include <unordered_map>

#include "base/random.hh"
#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/SlappCache.hh"
#include "sim/clocked_object.hh"

namespace gem5 {

/**
 * A very simple cache object. Has a fully-associative data store with random
 * replacement.
 * This cache is fully blocking (not non-blocking). Only a single request can
 * be outstanding at a time.
 * This cache is a writeback cache.
 */
class SlappCache : public ClockedObject {
private:
  /**
   * Port on the CPU-side that receives requests.
   * Mostly just forwards requests to the cache (owner)
   */
  class CPUSidePort : public ResponsePort {
  private:
    /// Since this is a vector port, need to know what number this one is
    int id;

    /// The object that owns this object (SlappCache)
    SlappCache *owner;

    // packet that is outstanding on the response port - set
    // if the cache tries to reply to the CPU but it is busy
    std::optional<PacketPtr> outstanding;

    /// True if the port needs to send a retry req.
    bool retry;

  public:
    /**
     * Constructor. Just calls the superclass constructor.
     */
    CPUSidePort(const std::string &name, int id, SlappCache *owner)
        : ResponsePort(name), id(id), owner(owner), outstanding(std::nullopt),
          retry(false) {}

    /**
     * Get a list of the non-overlapping address ranges the owner is
     * responsible for. All response ports must override this function
     * and return a populated list with at least one item.
     *
     * @return a list of ranges responded to
     */
    AddrRangeList getAddrRanges() const override;

    /**
     * Send a packet across this port. This is called by the owner and
     * all of the flow control is hanled in this function.
     * This is a convenience function for the SlappCache to send pkts.
     *
     * @param packet to send.
     */
    void sendPacket(PacketPtr pkt);

    /**
     * Send a retry to the peer port only if it is needed. This is called
     * from the SimpleCache whenever it is unblocked.
     */
    void trySendRetry();

  protected:
    /**
     * Receive an atomic request packet from the request port.
     * No need to implement in this slapp cache.
     */
    Tick recvAtomic(PacketPtr pkt) override { panic("recvAtomic unimpl."); }

    /**
     * Receive a functional request packet from the request port.
     * Performs a "debug" access updating/reading the data in place.
     *
     * @param packet the requestor sent.
     */
    void recvFunctional(PacketPtr pkt) override;

    /**
     * Receive a timing request from the request port.
     *
     * @param the packet that the requestor sent
     * @return whether this object can consume to packet. If false, we
     *         will call sendRetry() when we can try to receive this
     *         request again.
     */
    bool recvTimingReq(PacketPtr pkt) override;

    /**
     * Called by the request port if sendTimingResp was called on this
     * response port (causing recvTimingResp to be called on the request
     * port) and was unsuccessful.
     */
    void recvRespRetry() override;
  };

  /**
   * Port on the memory-side that receives responses.
   * Mostly just forwards requests to the cache (owner)
   */
  class MemSidePort : public RequestPort {
    friend class SlappCache;

  private:
    /// The object that owns this object (SlappCache)
    SlappCache *owner;

  public:
    /**
     * Constructor. Just calls the superclass constructor.
     */
    MemSidePort(const std::string &name, SlappCache *owner)
        : RequestPort(name), owner(owner), outstanding() {}

    /**
     * Processes a packet from the outstanding queue and tries to
     * send it across the port. This should be run following
     * flow control rules while outstanding is not empty to
     * make forward progress.
     *
     * @return true if queue is empty.
     */
    bool process();

    /// Queue of outstanding memory requests (read and write)
    /// that we need to service the cache request. Because this
    /// is a blocking cache, the queue is completely filled up
    /// when the request is made and then slowly consumed asynchronously
    /// (in order). When the queue is empty, the cache responds
    /// to the CPU.
    std::queue<PacketPtr> outstanding;

  protected:
    /**
     * Called to receive an address range change from the peer response
     * port. The default implementation ignores the change and does
     * nothing. Override this function in a derived class if the owner
     * needs to be aware of the address ranges, e.g. in an
     * interconnect component like a bus.
     */
    void recvRangeChange() override;

    /**
     * Receive a timing response from the response port.
     */
    bool recvTimingResp(PacketPtr pkt) override;

    /**
     * Called by the response port if sendTimingReq was called on this
     * request port (causing recvTimingReq to be called on the response
     * port) and was unsuccesful.
     */
    void recvReqRetry() override;
  };

  struct Meta {
    // Whether this tag is valid
    bool valid;
    // first address in the block
    Addr start;
    // last address in the block (inclusive)
    Addr end;
    // Pointer into the data heap
    size_t offset;

    Meta() = default;
    Meta(Addr start, unsigned size) : start(start), end(start + size - 1) {}
  };

  /**
   * Handle the request from the CPU side. Called from the CPU port
   * on a timing request.
   *
   * @param requesting packet
   * @param id of the port to send the response
   * @return true if we can handle the request this cycle, false if the
   *         requestor needs to retry later
   */
  bool handleTimingReq(PacketPtr pkt, int port_id);

  /**
   * Handle the respone from the memory side. Called from the memory port
   * on a timing response.
   *
   * @param responding packet
   * @return true if we can handle the response this cycle, false if the
   *         responder needs to retry later
   */
  bool handleTimingResp(PacketPtr pkt);

  /**
   * Send the packet to the CPU side.
   * This function assumes the pkt is already a response packet and forwards
   * it to the correct port. This function also unblocks this object and
   * cleans up the whole request.
   *
   * @param the packet to send to the cpu side
   */
  void sendResponse(PacketPtr pkt);

  /**
   * Handle a packet functionally. Update the data on a write and get the
   * data on a read. Called from CPU port on a recv functional.
   *
   * @param packet to functionally handle
   */
  void handleFunctional(PacketPtr pkt);

  /**
   * Access the cache for a timing access. This is called after the cache
   * access latency has already elapsed.
   */
  void accessTiming(PacketPtr pkt);

  /**
   * This is where we actually update / read from the cache. This function
   * is executed on both timing and functional accesses.
   *
   * @return true if a hit, false otherwise
   */
  bool accessFunctional(PacketPtr pkt);

  void evict(const uint64_t set_index, const uint64_t target);

  /**
   * Insert a block into the cache. If there is no room left in the cache,
   * then this function evicts a random entry t make room for the new block.
   *
   * @param packet with the data (and address) to insert into the cache
   */
  void insert(PacketPtr pkt);

  void allocate(PacketPtr pkt);

  /**
   * Return the address ranges this cache is responsible for. Just use the
   * same as the next upper level of the hierarchy.
   *
   * @return the address ranges this cache is responsible for
   */
  AddrRangeList getAddrRanges() const;

  /**
   * Tell the CPU side to ask for our memory ranges.
   */
  void sendRangeChange() const;

  /// Latency to check the cache. Number of cycles for both hit and miss
  const Cycles latency;

  /// The number of sets in the cache.
  const unsigned sets;

  /// The number of tags per set in the cache.
  const unsigned associativity;

  /// The total data capacity of the backing store.
  const size_t capacity;

  /// Instantiation of the CPU-side port
  std::vector<CPUSidePort> cpuPorts;

  /// Instantiation of the memory-side port
  MemSidePort memPort;

  /// True if this cache is currently blocked waiting for a response.
  bool blocked;

  /// Packet that we are currently handling. Used for upgrading to larger
  /// cache line sizes
  PacketPtr originalPacket;

  /// The port to send the response when we recieve it back
  int waitingPortId;

  /// For tracking the miss latency
  Tick missTime;

  /// Conventional cache metadata array with a fixed number
  /// of sets and associativity.
  std::vector<std::vector<Meta>> metadata;
  /// Data heap for the cache's backing store.
  std::vector<uint8_t> heap;
  /// Write pointer to end of heap.
  size_t writePointer;

  std::unordered_map<uint64_t, uint8_t> present;

  Random::RandomPtr rng = Random::genRandom();

  /// Cache statistics
protected:
  struct SlappCacheStats : public statistics::Group {
    SlappCacheStats(statistics::Group *parent);
    statistics::Scalar hits;
    statistics::Scalar misses;
    statistics::Histogram missLatency;
    statistics::Formula hitRatio;
  } stats;

public:
  /** constructor
   */
  SlappCache(const SlappCacheParams &params);

  /**
   * Get a port with a given name and index. This is used at
   * binding time and returns a reference to a protocol-agnostic
   * port.
   *
   * @param if_name Port name
   * @param idx Index in the case of a VectorPort
   *
   * @return A reference to the given port
   */
  Port &getPort(const std::string &if_name,
                PortID idx = InvalidPortID) override;
};

} // namespace gem5
