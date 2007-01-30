/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
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
 *
 * Authors: Erik Hallnor
 */

/**
 * @file
 * Declares a basic cache interface BaseCache.
 */

#ifndef __BASE_CACHE_HH__
#define __BASE_CACHE_HH__

#include <vector>
#include <string>
#include <list>
#include <inttypes.h>

#include "base/misc.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "mem/mem_object.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "mem/request.hh"
#include "sim/eventq.hh"

/**
 * Reasons for Caches to be Blocked.
 */
enum BlockedCause{
    Blocked_NoMSHRs,
    Blocked_NoTargets,
    Blocked_NoWBBuffers,
    Blocked_Coherence,
    NUM_BLOCKED_CAUSES
};

/**
 * Reasons for cache to request a bus.
 */
enum RequestCause{
    Request_MSHR,
    Request_WB,
    Request_Coherence,
    Request_PF
};

class MSHR;
/**
 * A basic cache interface. Implements some common functions for speed.
 */
class BaseCache : public MemObject
{
    class CachePort : public Port
    {
      public:
        BaseCache *cache;

      protected:
        CachePort(const std::string &_name, BaseCache *_cache, bool _isCpuSide);
        virtual void recvStatusChange(Status status);

        virtual void getDeviceAddressRanges(AddrRangeList &resp,
                                            AddrRangeList &snoop);

        virtual int deviceBlockSize();

        virtual void recvRetry();

      public:
        void setBlocked();

        void clearBlocked();

        bool checkFunctional(PacketPtr pkt);

        void checkAndSendFunctional(PacketPtr pkt);

        bool canDrain() { return drainList.empty() && transmitList.empty(); }

        bool blocked;

        bool mustSendRetry;

        bool isCpuSide;

        bool waitingOnRetry;

        std::list<PacketPtr> drainList;

        std::list<std::pair<Tick,PacketPtr> > transmitList;
    };

    struct CacheEvent : public Event
    {
        CachePort *cachePort;
        PacketPtr pkt;
        bool newResponse;

        CacheEvent(CachePort *_cachePort, bool response);
        void process();
        const char *description();
    };

  public: //Made public so coherence can get at it.
    CachePort *cpuSidePort;
    CachePort *memSidePort;

    CacheEvent *sendEvent;
    CacheEvent *memSendEvent;

  private:
    void recvStatusChange(Port::Status status, bool isCpuSide)
    {
        if (status == Port::RangeChange){
            if (!isCpuSide) {
                cpuSidePort->sendStatusChange(Port::RangeChange);
            }
            else {
                memSidePort->sendStatusChange(Port::RangeChange);
            }
        }
    }

    virtual PacketPtr getPacket() = 0;

    virtual PacketPtr getCoherencePacket() = 0;

    virtual void sendResult(PacketPtr &pkt, MSHR* mshr, bool success) = 0;

    virtual void sendCoherenceResult(PacketPtr &pkt, MSHR* mshr, bool success) = 0;

    /**
     * Bit vector of the blocking reasons for the access path.
     * @sa #BlockedCause
     */
    uint8_t blocked;

    /**
     * Bit vector for the blocking reasons for the snoop path.
     * @sa #BlockedCause
     */
    uint8_t blockedSnoop;

    /**
     * Bit vector for the outstanding requests for the master interface.
     */
    uint8_t masterRequests;

    /**
     * Bit vector for the outstanding requests for the slave interface.
     */
    uint8_t slaveRequests;

  protected:

    /** Stores time the cache blocked for statistics. */
    Tick blockedCycle;

    /** Block size of this cache */
    const int blkSize;

    /** The number of misses to trigger an exit event. */
    Counter missCount;

    /** The drain event. */
    Event *drainEvent;

  public:
    // Statistics
    /**
     * @addtogroup CacheStatistics
     * @{
     */

    /** Number of hits per thread for each type of command. @sa Packet::Command */
    Stats::Vector<> hits[NUM_MEM_CMDS];
    /** Number of hits for demand accesses. */
    Stats::Formula demandHits;
    /** Number of hit for all accesses. */
    Stats::Formula overallHits;

    /** Number of misses per thread for each type of command. @sa Packet::Command */
    Stats::Vector<> misses[NUM_MEM_CMDS];
    /** Number of misses for demand accesses. */
    Stats::Formula demandMisses;
    /** Number of misses for all accesses. */
    Stats::Formula overallMisses;

    /**
     * Total number of cycles per thread/command spent waiting for a miss.
     * Used to calculate the average miss latency.
     */
    Stats::Vector<> missLatency[NUM_MEM_CMDS];
    /** Total number of cycles spent waiting for demand misses. */
    Stats::Formula demandMissLatency;
    /** Total number of cycles spent waiting for all misses. */
    Stats::Formula overallMissLatency;

    /** The number of accesses per command and thread. */
    Stats::Formula accesses[NUM_MEM_CMDS];
    /** The number of demand accesses. */
    Stats::Formula demandAccesses;
    /** The number of overall accesses. */
    Stats::Formula overallAccesses;

    /** The miss rate per command and thread. */
    Stats::Formula missRate[NUM_MEM_CMDS];
    /** The miss rate of all demand accesses. */
    Stats::Formula demandMissRate;
    /** The miss rate for all accesses. */
    Stats::Formula overallMissRate;

    /** The average miss latency per command and thread. */
    Stats::Formula avgMissLatency[NUM_MEM_CMDS];
    /** The average miss latency for demand misses. */
    Stats::Formula demandAvgMissLatency;
    /** The average miss latency for all misses. */
    Stats::Formula overallAvgMissLatency;

    /** The total number of cycles blocked for each blocked cause. */
    Stats::Vector<> blocked_cycles;
    /** The number of times this cache blocked for each blocked cause. */
    Stats::Vector<> blocked_causes;

    /** The average number of cycles blocked for each blocked cause. */
    Stats::Formula avg_blocked;

    /** The number of fast writes (WH64) performed. */
    Stats::Scalar<> fastWrites;

    /** The number of cache copies performed. */
    Stats::Scalar<> cacheCopies;

    /**
     * @}
     */

    /**
     * Register stats for this object.
     */
    virtual void regStats();

  public:

    class Params
    {
      public:
        /** List of address ranges of this cache. */
        std::vector<Range<Addr> > addrRange;
        /** The hit latency for this cache. */
        int hitLatency;
        /** The block size of this cache. */
        int blkSize;
        /**
         * The maximum number of misses this cache should handle before
         * ending the simulation.
         */
        Counter maxMisses;

        /**
         * Construct an instance of this parameter class.
         */
        Params(std::vector<Range<Addr> > addr_range,
               int hit_latency, int _blkSize, Counter max_misses)
            : addrRange(addr_range), hitLatency(hit_latency), blkSize(_blkSize),
              maxMisses(max_misses)
        {
        }
    };

    /**
     * Create and initialize a basic cache object.
     * @param name The name of this cache.
     * @param hier_params Pointer to the HierParams object for this hierarchy
     * of this cache.
     * @param params The parameter object for this BaseCache.
     */
    BaseCache(const std::string &name, Params &params)
        : MemObject(name), blocked(0), blockedSnoop(0), masterRequests(0),
          slaveRequests(0), blkSize(params.blkSize),
          missCount(params.maxMisses), drainEvent(NULL)
    {
        //Start ports at null if more than one is created we should panic
        cpuSidePort = NULL;
        memSidePort = NULL;
    }

    ~BaseCache()
    {
        delete sendEvent;
        delete memSendEvent;
    }

    virtual void init();

    /**
     * Query block size of a cache.
     * @return  The block size
     */
    int getBlockSize() const
    {
        return blkSize;
    }

    /**
     * Returns true if the cache is blocked for accesses.
     */
    bool isBlocked()
    {
        return blocked != 0;
    }

    /**
     * Returns true if the cache is blocked for snoops.
     */
    bool isBlockedForSnoop()
    {
        return blockedSnoop != 0;
    }

    /**
     * Marks the access path of the cache as blocked for the given cause. This
     * also sets the blocked flag in the slave interface.
     * @param cause The reason for the cache blocking.
     */
    void setBlocked(BlockedCause cause)
    {
        uint8_t flag = 1 << cause;
        if (blocked == 0) {
            blocked_causes[cause]++;
            blockedCycle = curTick;
        }
        int old_state = blocked;
        if (!(blocked & flag)) {
            //Wasn't already blocked for this cause
            blocked |= flag;
            DPRINTF(Cache,"Blocking for cause %s\n", cause);
            if (!old_state)
                cpuSidePort->setBlocked();
        }
    }

    /**
     * Marks the snoop path of the cache as blocked for the given cause. This
     * also sets the blocked flag in the master interface.
     * @param cause The reason to block the snoop path.
     */
    void setBlockedForSnoop(BlockedCause cause)
    {
        uint8_t flag = 1 << cause;
        uint8_t old_state = blockedSnoop;
        if (!(blockedSnoop & flag)) {
            //Wasn't already blocked for this cause
            blockedSnoop |= flag;
            if (!old_state)
                memSidePort->setBlocked();
        }
    }

    /**
     * Marks the cache as unblocked for the given cause. This also clears the
     * blocked flags in the appropriate interfaces.
     * @param cause The newly unblocked cause.
     * @warning Calling this function can cause a blocked request on the bus to
     * access the cache. The cache must be in a state to handle that request.
     */
    void clearBlocked(BlockedCause cause)
    {
        uint8_t flag = 1 << cause;
        DPRINTF(Cache,"Unblocking for cause %s, causes left=%i\n",
                cause, blocked);
        if (blocked & flag)
        {
            blocked &= ~flag;
            if (!isBlocked()) {
                blocked_cycles[cause] += curTick - blockedCycle;
                DPRINTF(Cache,"Unblocking from all causes\n");
                cpuSidePort->clearBlocked();
            }
        }
        if (blockedSnoop & flag)
        {
            blockedSnoop &= ~flag;
            if (!isBlockedForSnoop()) {
                memSidePort->clearBlocked();
            }
        }
    }

    /**
     * True if the master bus should be requested.
     * @return True if there are outstanding requests for the master bus.
     */
    bool doMasterRequest()
    {
        return masterRequests != 0;
    }

    /**
     * Request the master bus for the given cause and time.
     * @param cause The reason for the request.
     * @param time The time to make the request.
     */
    void setMasterRequest(RequestCause cause, Tick time)
    {
        if (!doMasterRequest() && !memSidePort->waitingOnRetry)
        {
            BaseCache::CacheEvent * reqCpu =
                new BaseCache::CacheEvent(memSidePort, false);
            reqCpu->schedule(time);
        }
        uint8_t flag = 1<<cause;
        masterRequests |= flag;
    }

    /**
     * Clear the master bus request for the given cause.
     * @param cause The request reason to clear.
     */
    void clearMasterRequest(RequestCause cause)
    {
        uint8_t flag = 1<<cause;
        masterRequests &= ~flag;
        checkDrain();
    }

    /**
     * Return true if the slave bus should be requested.
     * @return True if there are outstanding requests for the slave bus.
     */
    bool doSlaveRequest()
    {
        return slaveRequests != 0;
    }

    /**
     * Request the slave bus for the given reason and time.
     * @param cause The reason for the request.
     * @param time The time to make the request.
     */
    void setSlaveRequest(RequestCause cause, Tick time)
    {
        if (!doSlaveRequest() && !cpuSidePort->waitingOnRetry)
        {
            BaseCache::CacheEvent * reqCpu =
                new BaseCache::CacheEvent(cpuSidePort, false);
            reqCpu->schedule(time);
        }
        uint8_t flag = 1<<cause;
        slaveRequests |= flag;
    }

    /**
     * Clear the slave bus request for the given reason.
     * @param cause The request reason to clear.
     */
    void clearSlaveRequest(RequestCause cause)
    {
        uint8_t flag = 1<<cause;
        slaveRequests &= ~flag;
        checkDrain();
    }

    /**
     * Send a response to the slave interface.
     * @param pkt The request being responded to.
     * @param time The time the response is ready.
     */
    void respond(PacketPtr pkt, Tick time)
    {
        assert(time >= curTick);
        if (pkt->needsResponse()) {
/*            CacheEvent *reqCpu = new CacheEvent(cpuSidePort, pkt);
            reqCpu->schedule(time);
*/
            if (cpuSidePort->transmitList.empty()) {
                assert(!sendEvent->scheduled());
                sendEvent->schedule(time);
                cpuSidePort->transmitList.push_back(std::pair<Tick,PacketPtr>
                                                    (time,pkt));
                return;
            }

            // something is on the list and this belongs at the end
            if (time >= cpuSidePort->transmitList.back().first) {
                cpuSidePort->transmitList.push_back(std::pair<Tick,PacketPtr>
                                                    (time,pkt));
                return;
            }
            // Something is on the list and this belongs somewhere else
            std::list<std::pair<Tick,PacketPtr> >::iterator i =
                cpuSidePort->transmitList.begin();
            std::list<std::pair<Tick,PacketPtr> >::iterator end =
                cpuSidePort->transmitList.end();
            bool done = false;

            while (i != end && !done) {
                if (time < i->first) {
                    if (i == cpuSidePort->transmitList.begin()) {
                        //Inserting at begining, reschedule
                        sendEvent->reschedule(time);
                    }
                    cpuSidePort->transmitList.insert(i,std::pair<Tick,PacketPtr>
                                                     (time,pkt));
                    done = true;
                }
                i++;
            }
        }
        else {
            if (pkt->cmd != Packet::UpgradeReq)
            {
                delete pkt->req;
                delete pkt;
            }
        }
    }

    /**
     * Send a reponse to the slave interface and calculate miss latency.
     * @param pkt The request to respond to.
     * @param time The time the response is ready.
     */
    void respondToMiss(PacketPtr pkt, Tick time)
    {
        assert(time >= curTick);
        if (!pkt->req->isUncacheable()) {
            missLatency[pkt->cmdToIndex()][0/*pkt->req->getThreadNum()*/] +=
                time - pkt->time;
        }
        if (pkt->needsResponse()) {
/*            CacheEvent *reqCpu = new CacheEvent(cpuSidePort, pkt);
            reqCpu->schedule(time);
*/
            if (cpuSidePort->transmitList.empty()) {
                assert(!sendEvent->scheduled());
                sendEvent->schedule(time);
                cpuSidePort->transmitList.push_back(std::pair<Tick,PacketPtr>
                                                    (time,pkt));
                return;
            }

            // something is on the list and this belongs at the end
            if (time >= cpuSidePort->transmitList.back().first) {
                cpuSidePort->transmitList.push_back(std::pair<Tick,PacketPtr>
                                                    (time,pkt));
                return;
            }
            // Something is on the list and this belongs somewhere else
            std::list<std::pair<Tick,PacketPtr> >::iterator i =
                cpuSidePort->transmitList.begin();
            std::list<std::pair<Tick,PacketPtr> >::iterator end =
                cpuSidePort->transmitList.end();
            bool done = false;

            while (i != end && !done) {
                if (time < i->first) {
                    if (i == cpuSidePort->transmitList.begin()) {
                        //Inserting at begining, reschedule
                        sendEvent->reschedule(time);
                    }
                    cpuSidePort->transmitList.insert(i,std::pair<Tick,PacketPtr>
                                                     (time,pkt));
                    done = true;
                }
                i++;
            }
        }
        else {
            if (pkt->cmd != Packet::UpgradeReq)
            {
                delete pkt->req;
                delete pkt;
            }
        }
    }

    /**
     * Suppliess the data if cache to cache transfers are enabled.
     * @param pkt The bus transaction to fulfill.
     */
    void respondToSnoop(PacketPtr pkt, Tick time)
    {
        assert(time >= curTick);
        assert (pkt->needsResponse());
/*        CacheEvent *reqMem = new CacheEvent(memSidePort, pkt);
        reqMem->schedule(time);
*/
        if (memSidePort->transmitList.empty()) {
            assert(!memSendEvent->scheduled());
            memSendEvent->schedule(time);
            memSidePort->transmitList.push_back(std::pair<Tick,PacketPtr>
                                                (time,pkt));
            return;
        }

        // something is on the list and this belongs at the end
        if (time >= memSidePort->transmitList.back().first) {
            memSidePort->transmitList.push_back(std::pair<Tick,PacketPtr>
                                                (time,pkt));
            return;
        }
        // Something is on the list and this belongs somewhere else
        std::list<std::pair<Tick,PacketPtr> >::iterator i =
            memSidePort->transmitList.begin();
        std::list<std::pair<Tick,PacketPtr> >::iterator end =
            memSidePort->transmitList.end();
        bool done = false;

        while (i != end && !done) {
            if (time < i->first) {
                if (i == memSidePort->transmitList.begin()) {
                    //Inserting at begining, reschedule
                    memSendEvent->reschedule(time);
                }
                memSidePort->transmitList.insert(i,std::pair<Tick,PacketPtr>(time,pkt));
                done = true;
            }
            i++;
        }
    }

    /**
     * Notification from master interface that a address range changed. Nothing
     * to do for a cache.
     */
    void rangeChange() {}

    void getAddressRanges(AddrRangeList &resp, AddrRangeList &snoop, bool isCpuSide)
    {
        if (isCpuSide)
        {
            AddrRangeList dummy;
            memSidePort->getPeerAddressRanges(resp, dummy);
        }
        else
        {
            //This is where snoops get updated
            AddrRangeList dummy;
            cpuSidePort->getPeerAddressRanges(dummy, snoop);
            return;
        }
    }

    virtual unsigned int drain(Event *de);

    void checkDrain()
    {
        if (drainEvent && canDrain()) {
            drainEvent->process();
            changeState(SimObject::Drained);
            // Clear the drain event
            drainEvent = NULL;
        }
    }

    bool canDrain()
    {
        if (doMasterRequest() || doSlaveRequest()) {
            return false;
        } else if (memSidePort && !memSidePort->canDrain()) {
            return false;
        } else if (cpuSidePort && !cpuSidePort->canDrain()) {
            return false;
        }
        return true;
    }

    virtual bool inCache(Addr addr) = 0;

    virtual bool inMissQueue(Addr addr) = 0;
};

#endif //__BASE_CACHE_HH__
