/*
 * Copyright (c) 2003 The Regents of The University of Michigan
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

#include "base/remote_gdb.hh"
#include "base/trace.hh"
#include "base/loader/aout_object.hh"
#include "base/loader/ecoff_object.hh"
#include "base/loader/object_file.hh"
#include "base/loader/symtab.hh"
#include "cpu/exec_context.hh"
#include "kern/tru64/tru64_events.hh"
#include "kern/tru64/tru64_system.hh"
#include "mem/functional_mem/memory_control.hh"
#include "mem/functional_mem/physical_memory.hh"
#include "targetarch/isa_traits.hh"
#include "targetarch/vtophys.hh"

using namespace std;

Tru64System::Tru64System(const string _name, MemoryController *_memCtrl,
                         PhysicalMemory *_physmem, const string &kernel_path,
                         const string &console_path, const string &palcode,
                         const string &boot_osflags)
     : System(_name, _memCtrl, _physmem)
{
    kernelSymtab = new SymbolTable;
    consoleSymtab = new SymbolTable;

    ObjectFile *kernel = createObjectFile(kernel_path);
    if (kernel == NULL)
        fatal("Could not load kernel file %s", kernel_path);

    ObjectFile *console = createObjectFile(console_path);
    if (console == NULL)
        fatal("Could not load console file %s", console_path);

    if (!kernel->loadGlobalSymbols(kernelSymtab))
        panic("could not load kernel symbols\n");

    if (!console->loadGlobalSymbols(consoleSymtab))
        panic("could not load console symbols\n");

    // Load pal file
    ObjectFile *pal = createObjectFile(palcode);
    if (pal == NULL)
        fatal("Could not load PALcode file %s", palcode);
    pal->loadSections(physmem, true);

    // copy of initial reg file contents
    initRegs = new RegFile;
    memset(initRegs, 0, sizeof(RegFile));

    // Load console file
    console->loadSections(physmem, true);

    // Load kernel file
    kernel->loadSections(physmem, true);
    kernelStart = kernel->textBase();
    kernelEnd = kernel->bssBase() + kernel->bssSize();
    kernelEntry = kernel->entryPoint();

    DPRINTF(Loader, "Kernel start = %#x\n"
            "Kernel end   = %#x\n"
            "Kernel entry = %#x\n",
            kernelStart, kernelEnd, kernelEntry);

    // Setup kernel boot parameters
    initRegs->pc = 0x4001;
    initRegs->npc = initRegs->pc + sizeof(MachInst);

    DPRINTF(Loader, "Kernel loaded...\n");

    kernelPanicEvent = new BreakPCEvent(&pcEventQueue, "kernel panic");
    consolePanicEvent = new BreakPCEvent(&pcEventQueue, "console panic");
    badaddrEvent = new BadAddrEvent(&pcEventQueue, "badaddr");
    skipPowerStateEvent = new SkipFuncEvent(&pcEventQueue,
                                            "tl_v48_capture_power_state");
    skipScavengeBootEvent = new SkipFuncEvent(&pcEventQueue,
                                              "pmap_scavenge_boot");
    printfEvent = new PrintfEvent(&pcEventQueue, "printf");
    debugPrintfEvent = new DebugPrintfEvent(&pcEventQueue,
                                            "debug_printf", false);
    debugPrintfrEvent = new DebugPrintfEvent(&pcEventQueue,
                                             "debug_printfr", true);
    dumpMbufEvent = new DumpMbufEvent(&pcEventQueue, "dump_mbuf");

    Addr addr = 0;
    if (kernelSymtab->findAddress("enable_async_printf", addr)) {
        Addr paddr = vtophys(physmem, addr);
        uint8_t *enable_async_printf =
            physmem->dma_addr(paddr, sizeof(uint32_t));

        if (enable_async_printf)
            *(uint32_t *)enable_async_printf = 0;
    }

    if (consoleSymtab->findAddress("env_booted_osflags", addr)) {
        Addr paddr = vtophys(physmem, addr);
        char *osflags = (char *)physmem->dma_addr(paddr, sizeof(uint32_t));

        if (osflags)
            strcpy(osflags, boot_osflags.c_str());
    }

    if (kernelSymtab->findAddress("panic", addr))
        kernelPanicEvent->schedule(addr);
    else
        panic("could not find kernel symbol \'panic\'");

    if (consoleSymtab->findAddress("panic", addr))
        consolePanicEvent->schedule(addr);

    if (kernelSymtab->findAddress("badaddr", addr))
        badaddrEvent->schedule(addr);
    else
        panic("could not find kernel symbol \'badaddr\'");

    if (kernelSymtab->findAddress("tl_v48_capture_power_state", addr))
        skipPowerStateEvent->schedule(addr);

    if (kernelSymtab->findAddress("pmap_scavenge_boot", addr))
        skipScavengeBootEvent->schedule(addr);

#if TRACING_ON
    if (kernelSymtab->findAddress("printf", addr))
        printfEvent->schedule(addr);

    if (kernelSymtab->findAddress("m5printf", addr))
        debugPrintfEvent->schedule(addr);

    if (kernelSymtab->findAddress("m5printfr", addr))
        debugPrintfrEvent->schedule(addr);

    if (kernelSymtab->findAddress("m5_dump_mbuf", addr))
        dumpMbufEvent->schedule(addr);
#endif
}

Tru64System::~Tru64System()
{
    delete initRegs;

    delete kernel;
    delete console;

    delete kernelSymtab;
    delete consoleSymtab;

    delete kernelPanicEvent;
    delete consolePanicEvent;
    delete badaddrEvent;
    delete skipPowerStateEvent;
    delete skipScavengeBootEvent;
    delete printfEvent;
    delete debugPrintfEvent;
    delete debugPrintfrEvent;
    delete dumpMbufEvent;

}

void
Tru64System::init(ExecContext *xc)
{
    xc->regs = *initRegs;

    remoteGDB = new RemoteGDB(this, xc);
    gdbListen = new GDBListener(remoteGDB, 7000);
    gdbListen->listen();

    // Reset the system
    //
    TheISA::init(physmem, &xc->regs);
}

bool
Tru64System::breakpoint()
{
    return remoteGDB->trap(ALPHA_KENTRY_IF);
}

BEGIN_DECLARE_SIM_OBJECT_PARAMS(Tru64System)

    SimObjectParam<MemoryController *> mem_ctl;
    SimObjectParam<PhysicalMemory *> physmem;

    Param<string> kernel_code;
    Param<string> console_code;
    Param<string> pal_code;
    Param<string> boot_osflags;

END_DECLARE_SIM_OBJECT_PARAMS(Tru64System)

BEGIN_INIT_SIM_OBJECT_PARAMS(Tru64System)

    INIT_PARAM(mem_ctl, "memory controller"),
    INIT_PARAM(physmem, "phsyical memory"),
    INIT_PARAM(kernel_code, "file that contains the kernel code"),
    INIT_PARAM(console_code, "file that contains the console code"),
    INIT_PARAM(pal_code, "file that contains palcode"),
    INIT_PARAM_DFLT(boot_osflags, "flags to pass to the kernel during boot",
                    "a")

END_INIT_SIM_OBJECT_PARAMS(Tru64System)

CREATE_SIM_OBJECT(Tru64System)
{
    Tru64System *sys = new Tru64System(getInstanceName(), mem_ctl, physmem,
                                       kernel_code, console_code, pal_code,
                                       boot_osflags);

    return sys;
}

REGISTER_SIM_OBJECT("Tru64System", Tru64System)
