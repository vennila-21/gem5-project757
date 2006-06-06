/*
 * Copyright (c) 2001-2006 The Regents of The University of Michigan
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
 * Authors: Steve Reinhardt
 *          Nathan Binkert
 *          Lisa Hsu
 *          Kevin Lim
 */

#include <string>

#include "arch/isa_traits.hh"
#include "cpu/base.hh"
#include "cpu/cpu_exec_context.hh"
#include "cpu/thread_context.hh"

#if FULL_SYSTEM
#include "base/callback.hh"
#include "base/cprintf.hh"
#include "base/output.hh"
#include "base/trace.hh"
#include "cpu/profile.hh"
#include "cpu/quiesce_event.hh"
#include "kern/kernel_stats.hh"
#include "sim/serialize.hh"
#include "sim/sim_exit.hh"
#include "arch/stacktrace.hh"
#else
#include "sim/process.hh"
#include "sim/system.hh"
#include "mem/translating_port.hh"
#endif

using namespace std;

// constructor
#if FULL_SYSTEM
CPUExecContext::CPUExecContext(BaseCPU *_cpu, int _thread_num, System *_sys,
                               AlphaITB *_itb, AlphaDTB *_dtb,
                               bool use_kernel_stats)
    : _status(ThreadContext::Unallocated), cpu(_cpu), thread_num(_thread_num),
      cpu_id(-1), lastActivate(0), lastSuspend(0), system(_sys), itb(_itb),
      dtb(_dtb), profile(NULL), func_exe_inst(0), storeCondFailures(0)

{
    tc = new ProxyThreadContext<CPUExecContext>(this);

    quiesceEvent = new EndQuiesceEvent(tc);

    regs.clear();

    if (cpu->params->profile) {
        profile = new FunctionProfile(system->kernelSymtab);
        Callback *cb =
            new MakeCallback<CPUExecContext,
            &CPUExecContext::dumpFuncProfile>(this);
        registerExitCallback(cb);
    }

    // let's fill with a dummy node for now so we don't get a segfault
    // on the first cycle when there's no node available.
    static ProfileNode dummyNode;
    profileNode = &dummyNode;
    profilePC = 3;


    if (use_kernel_stats) {
        kernelStats = new Kernel::Statistics(system);
    } else {
        kernelStats = NULL;
    }
    Port *mem_port;
    physPort = new FunctionalPort(csprintf("%s-%d-funcport",
                                           cpu->name(), thread_num));
    mem_port = system->physmem->getPort("functional");
    mem_port->setPeer(physPort);
    physPort->setPeer(mem_port);

    virtPort = new VirtualPort(csprintf("%s-%d-vport",
                                        cpu->name(), thread_num));
    mem_port = system->physmem->getPort("functional");
    mem_port->setPeer(virtPort);
    virtPort->setPeer(mem_port);
}
#else
CPUExecContext::CPUExecContext(BaseCPU *_cpu, int _thread_num,
                         Process *_process, int _asid, MemObject* memobj)
    : _status(ThreadContext::Unallocated),
      cpu(_cpu), thread_num(_thread_num), cpu_id(-1), lastActivate(0),
      lastSuspend(0), process(_process), asid(_asid),
      func_exe_inst(0), storeCondFailures(0)
{
    /* Use this port to for syscall emulation writes to memory. */
    Port *mem_port;
    port = new TranslatingPort(csprintf("%s-%d-funcport",
                                        cpu->name(), thread_num),
                               process->pTable, false);
    mem_port = memobj->getPort("functional");
    mem_port->setPeer(port);
    port->setPeer(mem_port);

    regs.clear();
    tc = new ProxyThreadContext<CPUExecContext>(this);
}

CPUExecContext::CPUExecContext(RegFile *regFile)
    : cpu(NULL), thread_num(-1), process(NULL), asid(-1),
      func_exe_inst(0), storeCondFailures(0)
{
    regs = *regFile;
    tc = new ProxyThreadContext<CPUExecContext>(this);
}

#endif

CPUExecContext::~CPUExecContext()
{
    delete tc;
}

#if FULL_SYSTEM
void
CPUExecContext::dumpFuncProfile()
{
    std::ostream *os = simout.create(csprintf("profile.%s.dat", cpu->name()));
    profile->dump(tc, *os);
}

void
CPUExecContext::profileClear()
{
    if (profile)
        profile->clear();
}

void
CPUExecContext::profileSample()
{
    if (profile)
        profile->sample(profileNode, profilePC);
}

#endif

void
CPUExecContext::takeOverFrom(ThreadContext *oldContext)
{
    // some things should already be set up
#if FULL_SYSTEM
    assert(system == oldContext->getSystemPtr());
#else
    assert(process == oldContext->getProcessPtr());
#endif

    // copy over functional state
    _status = oldContext->status();
    copyArchRegs(oldContext);
    cpu_id = oldContext->readCpuId();
#if !FULL_SYSTEM
    func_exe_inst = oldContext->readFuncExeInst();
#else
    EndQuiesceEvent *quiesce = oldContext->getQuiesceEvent();
    if (quiesce) {
        // Point the quiesce event's TC at this TC so that it wakes up
        // the proper CPU.
        quiesce->tc = tc;
    }
    if (quiesceEvent) {
        quiesceEvent->tc = tc;
    }
#endif

    storeCondFailures = 0;

    oldContext->setStatus(ThreadContext::Unallocated);
}

void
CPUExecContext::serialize(ostream &os)
{
    SERIALIZE_ENUM(_status);
    regs.serialize(os);
    // thread_num and cpu_id are deterministic from the config
    SERIALIZE_SCALAR(func_exe_inst);
    SERIALIZE_SCALAR(inst);

#if FULL_SYSTEM
    Tick quiesceEndTick = 0;
    if (quiesceEvent->scheduled())
        quiesceEndTick = quiesceEvent->when();
    SERIALIZE_SCALAR(quiesceEndTick);
    if (kernelStats)
        kernelStats->serialize(os);
#endif
}


void
CPUExecContext::unserialize(Checkpoint *cp, const std::string &section)
{
    UNSERIALIZE_ENUM(_status);
    regs.unserialize(cp, section);
    // thread_num and cpu_id are deterministic from the config
    UNSERIALIZE_SCALAR(func_exe_inst);
    UNSERIALIZE_SCALAR(inst);

#if FULL_SYSTEM
    Tick quiesceEndTick;
    UNSERIALIZE_SCALAR(quiesceEndTick);
    if (quiesceEndTick)
        quiesceEvent->schedule(quiesceEndTick);
    if (kernelStats)
        kernelStats->unserialize(cp, section);
#endif
}


void
CPUExecContext::activate(int delay)
{
    if (status() == ThreadContext::Active)
        return;

    lastActivate = curTick;

    if (status() == ThreadContext::Unallocated) {
        cpu->activateWhenReady(thread_num);
        return;
    }

    _status = ThreadContext::Active;

    // status() == Suspended
    cpu->activateContext(thread_num, delay);
}

void
CPUExecContext::suspend()
{
    if (status() == ThreadContext::Suspended)
        return;

    lastActivate = curTick;
    lastSuspend = curTick;
/*
#if FULL_SYSTEM
    // Don't change the status from active if there are pending interrupts
    if (cpu->check_interrupts()) {
        assert(status() == ThreadContext::Active);
        return;
    }
#endif
*/
    _status = ThreadContext::Suspended;
    cpu->suspendContext(thread_num);
}

void
CPUExecContext::deallocate()
{
    if (status() == ThreadContext::Unallocated)
        return;

    _status = ThreadContext::Unallocated;
    cpu->deallocateContext(thread_num);
}

void
CPUExecContext::halt()
{
    if (status() == ThreadContext::Halted)
        return;

    _status = ThreadContext::Halted;
    cpu->haltContext(thread_num);
}


void
CPUExecContext::regStats(const string &name)
{
#if FULL_SYSTEM
    if (kernelStats)
        kernelStats->regStats(name + ".kern");
#endif
}

void
CPUExecContext::copyArchRegs(ThreadContext *src_tc)
{
    TheISA::copyRegs(src_tc, tc);
}

#if FULL_SYSTEM
VirtualPort*
CPUExecContext::getVirtPort(ThreadContext *src_tc)
{
    if (!src_tc)
        return virtPort;

    VirtualPort *vp;
    Port *mem_port;

    vp = new VirtualPort("tc-vport", src_tc);
    mem_port = system->physmem->getPort("functional");
    mem_port->setPeer(vp);
    vp->setPeer(mem_port);
    return vp;
}

void
CPUExecContext::delVirtPort(VirtualPort *vp)
{
//    assert(!vp->nullThreadContext());
    delete vp->getPeer();
    delete vp;
}


#endif

