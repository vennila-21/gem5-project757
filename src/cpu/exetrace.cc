/*
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
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
 *          Lisa Hsu
 *          Nathan Binkert
 *          Steve Raasch
 */

#include <errno.h>
#include <fstream>
#include <iomanip>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "arch/predecoder.hh"
#include "arch/regfile.hh"
#include "arch/utility.hh"
#include "base/loader/symtab.hh"
#include "base/socket.hh"
#include "config/full_system.hh"
#include "cpu/base.hh"
#include "cpu/exetrace.hh"
#include "cpu/static_inst.hh"
#include "sim/param.hh"
#include "sim/system.hh"

#if FULL_SYSTEM
#include "arch/tlb.hh"
#endif

//XXX This is temporary
#include "arch/isa_specific.hh"
#include "cpu/m5legion_interface.h"

using namespace std;
using namespace TheISA;

#if THE_ISA == SPARC_ISA && FULL_SYSTEM
static int diffcount = 0;
static bool wasMicro = false;
#endif

namespace Trace {
SharedData *shared_data = NULL;
ListenSocket *cosim_listener = NULL;

void
setupSharedData()
{
    int shmfd = shmget('M' << 24 | getuid(), sizeof(SharedData), 0777);
    if (shmfd < 0)
        fatal("Couldn't get shared memory fd. Is Legion running?");

    shared_data = (SharedData*)shmat(shmfd, NULL, SHM_RND);
    if (shared_data == (SharedData*)-1)
        fatal("Couldn't allocate shared memory");

    if (shared_data->flags != OWN_M5)
        fatal("Shared memory has invalid owner");

    if (shared_data->version != VERSION)
        fatal("Shared Data is wrong version! M5: %d Legion: %d", VERSION,
              shared_data->version);

    // step legion forward one cycle so we can get register values
    shared_data->flags = OWN_LEGION;
}

////////////////////////////////////////////////////////////////////////
//
//  Methods for the InstRecord object
//

#if THE_ISA == SPARC_ISA

inline char * genCenteredLabel(int length, char * buffer, char * label)
{
    int labelLength = strlen(label);
    assert(labelLength <= length);
    int leftPad = (length - labelLength) / 2;
    int rightPad = length - leftPad - labelLength;
    char format[64];
    sprintf(format, "%%%ds%%s%%%ds", leftPad, rightPad);
    sprintf(buffer, format, "", label, "");
    return buffer;
}

inline void printRegPair(ostream & os, char const * title, uint64_t a, uint64_t b)
{
    ccprintf(os, "  %16s  |  %#018x   %s   %#-018x  \n",
            title, a, (a == b) ? "|" : "X", b);
}

inline void printColumnLabels(ostream & os)
{
    static char * regLabel = genCenteredLabel(16, new char[17], "Register");
    static char * m5Label = genCenteredLabel(18, new char[18], "M5");
    static char * legionLabel = genCenteredLabel(18, new char[18], "Legion");
    ccprintf(os, "  %s  |  %s   |   %s  \n", regLabel, m5Label, legionLabel);
    ccprintf(os, "--------------------+-----------------------+-----------------------\n");
}

inline void printSectionHeader(ostream & os, char * name)
{
    char sectionString[70];
    genCenteredLabel(69, sectionString, name);
    ccprintf(os, "====================================================================\n");
    ccprintf(os, "%69s\n", sectionString);
    ccprintf(os, "====================================================================\n");
}

inline void printLevelHeader(ostream & os, int level)
{
    char sectionString[70];
    char levelName[70];
    sprintf(levelName, "Trap stack level %d", level);
    genCenteredLabel(69, sectionString, levelName);
    ccprintf(os, "====================================================================\n");
    ccprintf(os, "%69s\n", sectionString);
    ccprintf(os, "====================================================================\n");
}

#endif

void
Trace::InstRecord::dump()
{
    ostream &outs = Trace::output();

    DPRINTF(Sparc, "Instruction: %#X\n", staticInst->machInst);
    bool diff = true;
    if (IsOn(ExecRegDelta))
    {
        diff = false;
#ifndef NDEBUG
#if THE_ISA == SPARC_ISA
        static int fd = 0;
        //Don't print what happens for each micro-op, just print out
        //once at the last op, and for regular instructions.
        if(!staticInst->isMicroop() || staticInst->isLastMicroop())
        {
            if(!cosim_listener)
            {
                int port = 8000;
                cosim_listener = new ListenSocket();
                while(!cosim_listener->listen(port, true))
                {
                    DPRINTF(GDBMisc, "Can't bind port %d\n", port);
                    port++;
                }
                ccprintf(cerr, "Listening for cosimulator on port %d\n", port);
                fd = cosim_listener->accept();
            }
            char prefix[] = "goli";
            for(int p = 0; p < 4; p++)
            {
                for(int i = 0; i < 8; i++)
                {
                    uint64_t regVal;
                    int res = read(fd, &regVal, sizeof(regVal));
                    if(res < 0)
                        panic("First read call failed! %s\n", strerror(errno));
                    regVal = TheISA::gtoh(regVal);
                    uint64_t realRegVal = thread->readIntReg(p * 8 + i);
                    if((regVal & 0xffffffffULL) != (realRegVal & 0xffffffffULL))
                    {
                        DPRINTF(ExecRegDelta, "Register %s%d should be %#x but is %#x.\n", prefix[p], i, regVal, realRegVal);
                        diff = true;
                    }
                    //ccprintf(outs, "%s%d m5 = %#x statetrace = %#x\n", prefix[p], i, realRegVal, regVal);
                }
            }
            /*for(int f = 0; f <= 62; f+=2)
            {
                uint64_t regVal;
                int res = read(fd, &regVal, sizeof(regVal));
                if(res < 0)
                    panic("First read call failed! %s\n", strerror(errno));
                regVal = TheISA::gtoh(regVal);
                uint64_t realRegVal = thread->readFloatRegBits(f, 64);
                if(regVal != realRegVal)
                {
                    DPRINTF(ExecRegDelta, "Register f%d should be %#x but is %#x.\n", f, regVal, realRegVal);
                }
            }*/
            uint64_t regVal;
            int res = read(fd, &regVal, sizeof(regVal));
            if(res < 0)
                panic("First read call failed! %s\n", strerror(errno));
            regVal = TheISA::gtoh(regVal);
            uint64_t realRegVal = thread->readNextPC();
            if(regVal != realRegVal)
            {
                DPRINTF(ExecRegDelta, "Register pc should be %#x but is %#x.\n", regVal, realRegVal);
                diff = true;
            }
            res = read(fd, &regVal, sizeof(regVal));
            if(res < 0)
                panic("First read call failed! %s\n", strerror(errno));
            regVal = TheISA::gtoh(regVal);
            realRegVal = thread->readNextNPC();
            if(regVal != realRegVal)
            {
                DPRINTF(ExecRegDelta, "Register npc should be %#x but is %#x.\n", regVal, realRegVal);
                diff = true;
            }
            res = read(fd, &regVal, sizeof(regVal));
            if(res < 0)
                panic("First read call failed! %s\n", strerror(errno));
            regVal = TheISA::gtoh(regVal);
            realRegVal = thread->readIntReg(SparcISA::NumIntArchRegs + 2);
            if((regVal & 0xF) != (realRegVal & 0xF))
            {
                DPRINTF(ExecRegDelta, "Register ccr should be %#x but is %#x.\n", regVal, realRegVal);
                diff = true;
            }
        }
#endif
#endif
#if 0 //THE_ISA == SPARC_ISA
        //Don't print what happens for each micro-op, just print out
        //once at the last op, and for regular instructions.
        if(!staticInst->isMicroop() || staticInst->isLastMicroop())
        {
            static uint64_t regs[32] = {
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0};
            static uint64_t ccr = 0;
            static uint64_t y = 0;
            static uint64_t floats[32];
            uint64_t newVal;
            static const char * prefixes[4] = {"G", "O", "L", "I"};

            outs << hex;
            outs << "PC = " << thread->readNextPC();
            outs << " NPC = " << thread->readNextNPC();
            newVal = thread->readIntReg(SparcISA::NumIntArchRegs + 2);
            //newVal = thread->readMiscRegNoEffect(SparcISA::MISCREG_CCR);
            if(newVal != ccr)
            {
                outs << " CCR = " << newVal;
                ccr = newVal;
            }
            newVal = thread->readIntReg(SparcISA::NumIntArchRegs + 1);
            //newVal = thread->readMiscRegNoEffect(SparcISA::MISCREG_Y);
            if(newVal != y)
            {
                outs << " Y = " << newVal;
                y = newVal;
            }
            for(int y = 0; y < 4; y++)
            {
                for(int x = 0; x < 8; x++)
                {
                    int index = x + 8 * y;
                    newVal = thread->readIntReg(index);
                    if(regs[index] != newVal)
                    {
                        outs << " " << prefixes[y] << dec << x << " = " << hex << newVal;
                        regs[index] = newVal;
                    }
                }
            }
            for(int y = 0; y < 32; y++)
            {
                newVal = thread->readFloatRegBits(2 * y, 64);
                if(floats[y] != newVal)
                {
                    outs << " F" << dec << (2 * y) << " = " << hex << newVal;
                    floats[y] = newVal;
                }
            }
            outs << dec << endl;
        }
#endif
    }
    if(!diff) {
    } else if (IsOn(ExecIntel)) {
        ccprintf(outs, "%7d ) ", when);
        outs << "0x" << hex << PC << ":\t";
        if (staticInst->isLoad()) {
            ccprintf(outs, "<RD %#x>", addr);
        } else if (staticInst->isStore()) {
            ccprintf(outs, "<WR %#x>", addr);
        }
        outs << endl;
    } else {
        if (IsOn(ExecTicks))
            ccprintf(outs, "%7d: ", when);

        outs << thread->getCpuPtr()->name() << " ";

        if (IsOn(ExecSpeculative))
            outs << (misspeculating ? "-" : "+") << " ";

        if (IsOn(ExecThread))
            outs << "T" << thread->getThreadNum() << " : ";


        std::string sym_str;
        Addr sym_addr;
        if (debugSymbolTable
            && debugSymbolTable->findNearestSymbol(PC, sym_str, sym_addr)
            && IsOn(ExecSymbol)) {
            if (PC != sym_addr)
                sym_str += csprintf("+%d", PC - sym_addr);
            outs << "@" << sym_str << " : ";
        }
        else {
            outs << "0x" << hex << PC << " : ";
        }

        //
        //  Print decoded instruction
        //

#if defined(__GNUC__) && (__GNUC__ < 3)
        // There's a bug in gcc 2.x library that prevents setw()
        // from working properly on strings
        string mc(staticInst->disassemble(PC, debugSymbolTable));
        while (mc.length() < 26)
            mc += " ";
        outs << mc;
#else
        outs << setw(26) << left << staticInst->disassemble(PC, debugSymbolTable);
#endif

        outs << " : ";

        if (IsOn(ExecOpClass)) {
            outs << opClassStrings[staticInst->opClass()] << " : ";
        }

        if (IsOn(ExecResult) && data_status != DataInvalid) {
            outs << " D=";
#if 0
            if (data_status == DataDouble)
                ccprintf(outs, "%f", data.as_double);
            else
                ccprintf(outs, "%#018x", data.as_int);
#else
            ccprintf(outs, "%#018x", data.as_int);
#endif
        }

        if (IsOn(ExecEffAddr) && addr_valid)
            outs << " A=0x" << hex << addr;

        if (IsOn(ExecIntRegs) && regs_valid) {
            for (int i = 0; i < TheISA::NumIntRegs;)
                for (int j = i + 1; i <= j; i++)
                    ccprintf(outs, "r%02d = %#018x%s", i,
                            iregs->regs.readReg(i),
                            ((i == j) ? "\n" : "    "));
            outs << "\n";
        }

        if (IsOn(ExecFetchSeq) && fetch_seq_valid)
            outs << "  FetchSeq=" << dec << fetch_seq;

        if (IsOn(ExecCPSeq) && cp_seq_valid)
            outs << "  CPSeq=" << dec << cp_seq;

        //
        //  End of line...
        //
        outs << endl;
    }
#if THE_ISA == SPARC_ISA && FULL_SYSTEM
    static TheISA::Predecoder predecoder(NULL);
    // Compare
    if (IsOn(ExecLegion))
    {
        bool compared = false;
        bool diffPC   = false;
        bool diffCC   = false;
        bool diffInst = false;
        bool diffIntRegs = false;
        bool diffFpRegs = false;
        bool diffTpc = false;
        bool diffTnpc = false;
        bool diffTstate = false;
        bool diffTt = false;
        bool diffTba = false;
        bool diffHpstate = false;
        bool diffHtstate = false;
        bool diffHtba = false;
        bool diffPstate = false;
        bool diffY = false;
        bool diffFsr = false;
        bool diffCcr = false;
        bool diffTl = false;
        bool diffGl = false;
        bool diffAsi = false;
        bool diffPil = false;
        bool diffCwp = false;
        bool diffCansave = false;
        bool diffCanrestore = false;
        bool diffOtherwin = false;
        bool diffCleanwin = false;
        bool diffTlb = false;
        Addr m5Pc, lgnPc;

        if (!shared_data)
            setupSharedData();

        // We took a trap on a micro-op...
        if (wasMicro && !staticInst->isMicroop())
        {
            // let's skip comparing this tick
            while (!compared)
                if (shared_data->flags == OWN_M5) {
                    shared_data->flags = OWN_LEGION;
                    compared = true;
                }
            compared = false;
            wasMicro = false;
        }

        if (staticInst->isLastMicroop())
            wasMicro = false;
        else if (staticInst->isMicroop())
            wasMicro = true;


        if(!staticInst->isMicroop() || staticInst->isLastMicroop()) {
            while (!compared) {
                if (shared_data->flags == OWN_M5) {
                    m5Pc = PC & TheISA::PAddrImplMask;
                    if (bits(shared_data->pstate,3,3)) {
                        m5Pc &= mask(32);
                    }
                    lgnPc = shared_data->pc & TheISA::PAddrImplMask;
                    if (lgnPc != m5Pc)
                       diffPC = true;

                    if (shared_data->cycle_count !=
                            thread->getCpuPtr()->instCount())
                        diffCC = true;

                    if (shared_data->instruction !=
                            (SparcISA::MachInst)staticInst->machInst) {
                        diffInst = true;
                    }
                    // assume we have %g0 working correctly
                    for (int i = 1; i < TheISA::NumIntArchRegs; i++) {
                        if (thread->readIntReg(i) != shared_data->intregs[i]) {
                            diffIntRegs = true;
                        }
                    }
                    for (int i = 0; i < TheISA::NumFloatRegs/2; i++) {
                        if (thread->readFloatRegBits(i*2,FloatRegFile::DoubleWidth) != shared_data->fpregs[i]) {
                            diffFpRegs = true;
                        }
                    }
                            uint64_t oldTl = thread->readMiscRegNoEffect(MISCREG_TL);
                    if (oldTl != shared_data->tl)
                        diffTl = true;
                    for (int i = 1; i <= MaxTL; i++) {
                        thread->setMiscRegNoEffect(MISCREG_TL, i);
                        if (thread->readMiscRegNoEffect(MISCREG_TPC) !=
                                shared_data->tpc[i-1])
                            diffTpc = true;
                        if (thread->readMiscRegNoEffect(MISCREG_TNPC) !=
                                shared_data->tnpc[i-1])
                            diffTnpc = true;
                        if (thread->readMiscRegNoEffect(MISCREG_TSTATE) !=
                                shared_data->tstate[i-1])
                            diffTstate = true;
                        if (thread->readMiscRegNoEffect(MISCREG_TT) !=
                                shared_data->tt[i-1])
                            diffTt = true;
                        if (thread->readMiscRegNoEffect(MISCREG_HTSTATE) !=
                                shared_data->htstate[i-1])
                            diffHtstate = true;
                    }
                    thread->setMiscRegNoEffect(MISCREG_TL, oldTl);

                    if(shared_data->tba != thread->readMiscRegNoEffect(MISCREG_TBA))
                        diffTba = true;
                    //When the hpstate register is read by an instruction,
                    //legion has bit 11 set. When it's in storage, it doesn't.
                    //Since we don't directly support seperate interpretations
                    //of the registers like that, the bit is always set to 1 and
                    //we just don't compare it. It's not supposed to matter
                    //anyway.
                    if((shared_data->hpstate | (1 << 11)) != thread->readMiscRegNoEffect(MISCREG_HPSTATE))
                        diffHpstate = true;
                    if(shared_data->htba != thread->readMiscRegNoEffect(MISCREG_HTBA))
                        diffHtba = true;
                    if(shared_data->pstate != thread->readMiscRegNoEffect(MISCREG_PSTATE))
                        diffPstate = true;
                    //if(shared_data->y != thread->readMiscRegNoEffect(MISCREG_Y))
                    if(shared_data->y !=
                            thread->readIntReg(NumIntArchRegs + 1))
                        diffY = true;
                    if(shared_data->fsr != thread->readMiscRegNoEffect(MISCREG_FSR)) {
                        diffFsr = true;
                        if (mbits(shared_data->fsr, 63,10) ==
                                mbits(thread->readMiscRegNoEffect(MISCREG_FSR), 63,10)) {
                            thread->setMiscRegNoEffect(MISCREG_FSR, shared_data->fsr);
                            diffFsr = false;
                        }
                    }
                    //if(shared_data->ccr != thread->readMiscRegNoEffect(MISCREG_CCR))
                    if(shared_data->ccr !=
                            thread->readIntReg(NumIntArchRegs + 2))
                        diffCcr = true;
                    if(shared_data->gl != thread->readMiscRegNoEffect(MISCREG_GL))
                        diffGl = true;
                    if(shared_data->asi != thread->readMiscRegNoEffect(MISCREG_ASI))
                        diffAsi = true;
                    if(shared_data->pil != thread->readMiscRegNoEffect(MISCREG_PIL))
                        diffPil = true;
                    if(shared_data->cwp != thread->readMiscRegNoEffect(MISCREG_CWP))
                        diffCwp = true;
                    //if(shared_data->cansave != thread->readMiscRegNoEffect(MISCREG_CANSAVE))
                    if(shared_data->cansave !=
                            thread->readIntReg(NumIntArchRegs + 3))
                        diffCansave = true;
                    //if(shared_data->canrestore !=
                    //	    thread->readMiscRegNoEffect(MISCREG_CANRESTORE))
                    if(shared_data->canrestore !=
                            thread->readIntReg(NumIntArchRegs + 4))
                        diffCanrestore = true;
                    //if(shared_data->otherwin != thread->readMiscRegNoEffect(MISCREG_OTHERWIN))
                    if(shared_data->otherwin !=
                            thread->readIntReg(NumIntArchRegs + 6))
                        diffOtherwin = true;
                    //if(shared_data->cleanwin != thread->readMiscRegNoEffect(MISCREG_CLEANWIN))
                    if(shared_data->cleanwin !=
                            thread->readIntReg(NumIntArchRegs + 5))
                        diffCleanwin = true;

                    for (int i = 0; i < 64; i++) {
                        if (shared_data->itb[i] !=  thread->getITBPtr()->TteRead(i))
                                diffTlb = true;
                        if (shared_data->dtb[i] !=  thread->getDTBPtr()->TteRead(i))
                                diffTlb = true;
                    }

                    if (diffPC || diffCC || diffInst || diffIntRegs ||
                         diffFpRegs || diffTpc || diffTnpc || diffTstate ||
                         diffTt || diffHpstate || diffHtstate || diffHtba ||
                         diffPstate || diffY || diffCcr || diffTl || diffFsr ||
                         diffGl || diffAsi || diffPil || diffCwp || diffCansave ||
                         diffCanrestore || diffOtherwin || diffCleanwin || diffTlb)
                       {

                        outs << "Differences found between M5 and Legion:";
                        if (diffPC)
                            outs << " [PC]";
                        if (diffCC)
                            outs << " [CC]";
                        if (diffInst)
                            outs << " [Instruction]";
                        if (diffIntRegs)
                            outs << " [IntRegs]";
                        if (diffFpRegs)
                            outs << " [FpRegs]";
                        if (diffTpc)
                            outs << " [Tpc]";
                        if (diffTnpc)
                            outs << " [Tnpc]";
                        if (diffTstate)
                            outs << " [Tstate]";
                        if (diffTt)
                            outs << " [Tt]";
                        if (diffHpstate)
                            outs << " [Hpstate]";
                        if (diffHtstate)
                            outs << " [Htstate]";
                        if (diffHtba)
                            outs << " [Htba]";
                        if (diffPstate)
                            outs << " [Pstate]";
                        if (diffY)
                            outs << " [Y]";
                        if (diffFsr)
                            outs << " [FSR]";
                        if (diffCcr)
                            outs << " [Ccr]";
                        if (diffTl)
                            outs << " [Tl]";
                        if (diffGl)
                            outs << " [Gl]";
                        if (diffAsi)
                            outs << " [Asi]";
                        if (diffPil)
                            outs << " [Pil]";
                        if (diffCwp)
                            outs << " [Cwp]";
                        if (diffCansave)
                            outs << " [Cansave]";
                        if (diffCanrestore)
                            outs << " [Canrestore]";
                        if (diffOtherwin)
                            outs << " [Otherwin]";
                        if (diffCleanwin)
                            outs << " [Cleanwin]";
                        if (diffTlb)
                            outs << " [Tlb]";
                        outs << endl << endl;

                        outs << right << setfill(' ') << setw(15)
                             << "M5 PC: " << "0x"<< setw(16) << setfill('0')
                             << hex << m5Pc << endl;
                        outs << setfill(' ') << setw(15)
                             << "Legion PC: " << "0x"<< setw(16) << setfill('0') << hex
                             << lgnPc << endl << endl;

                        outs << right << setfill(' ') << setw(15)
                             << "M5 CC: " << "0x"<< setw(16) << setfill('0')
                             << hex << thread->getCpuPtr()->instCount() << endl;
                        outs << setfill(' ') << setw(15)
                             << "Legion CC: " << "0x"<< setw(16) << setfill('0') << hex
                             << shared_data->cycle_count << endl << endl;

                        outs << setfill(' ') << setw(15)
                             << "M5 Inst: "  << "0x"<< setw(8)
                             << setfill('0') << hex << staticInst->machInst
                             << staticInst->disassemble(m5Pc, debugSymbolTable)
                             << endl;

                        predecoder.setTC(thread);
                        predecoder.moreBytes(m5Pc, m5Pc, 0,
                                shared_data->instruction);

                        assert(predecoder.extMachInstReady());

                        StaticInstPtr legionInst =
                            StaticInst::decode(predecoder.getExtMachInst());
                        outs << setfill(' ') << setw(15)
                             << " Legion Inst: "
                             << "0x" << setw(8) << setfill('0') << hex
                             << shared_data->instruction
                             << legionInst->disassemble(lgnPc, debugSymbolTable)
                             << endl << endl;

                        printSectionHeader(outs, "General State");
                        printColumnLabels(outs);
                        printRegPair(outs, "HPstate",
                                thread->readMiscRegNoEffect(MISCREG_HPSTATE),
                                shared_data->hpstate | (1 << 11));
                        printRegPair(outs, "Htba",
                                thread->readMiscRegNoEffect(MISCREG_HTBA),
                                shared_data->htba);
                        printRegPair(outs, "Pstate",
                                thread->readMiscRegNoEffect(MISCREG_PSTATE),
                                shared_data->pstate);
                        printRegPair(outs, "Y",
                                //thread->readMiscRegNoEffect(MISCREG_Y),
                                thread->readIntReg(NumIntArchRegs + 1),
                                shared_data->y);
                        printRegPair(outs, "FSR",
                                thread->readMiscRegNoEffect(MISCREG_FSR),
                                shared_data->fsr);
                        printRegPair(outs, "Ccr",
                                //thread->readMiscRegNoEffect(MISCREG_CCR),
                                thread->readIntReg(NumIntArchRegs + 2),
                                shared_data->ccr);
                        printRegPair(outs, "Tl",
                                thread->readMiscRegNoEffect(MISCREG_TL),
                                shared_data->tl);
                        printRegPair(outs, "Gl",
                                thread->readMiscRegNoEffect(MISCREG_GL),
                                shared_data->gl);
                        printRegPair(outs, "Asi",
                                thread->readMiscRegNoEffect(MISCREG_ASI),
                                shared_data->asi);
                        printRegPair(outs, "Pil",
                                thread->readMiscRegNoEffect(MISCREG_PIL),
                                shared_data->pil);
                        printRegPair(outs, "Cwp",
                                thread->readMiscRegNoEffect(MISCREG_CWP),
                                shared_data->cwp);
                        printRegPair(outs, "Cansave",
                                //thread->readMiscRegNoEffect(MISCREG_CANSAVE),
                                thread->readIntReg(NumIntArchRegs + 3),
                                shared_data->cansave);
                        printRegPair(outs, "Canrestore",
                                //thread->readMiscRegNoEffect(MISCREG_CANRESTORE),
                                thread->readIntReg(NumIntArchRegs + 4),
                                shared_data->canrestore);
                        printRegPair(outs, "Otherwin",
                                //thread->readMiscRegNoEffect(MISCREG_OTHERWIN),
                                thread->readIntReg(NumIntArchRegs + 6),
                                shared_data->otherwin);
                        printRegPair(outs, "Cleanwin",
                                //thread->readMiscRegNoEffect(MISCREG_CLEANWIN),
                                thread->readIntReg(NumIntArchRegs + 5),
                                shared_data->cleanwin);
                        outs << endl;
                        for (int i = 1; i <= MaxTL; i++) {
                            printLevelHeader(outs, i);
                            printColumnLabels(outs);
                            thread->setMiscRegNoEffect(MISCREG_TL, i);
                            printRegPair(outs, "Tpc",
                                    thread->readMiscRegNoEffect(MISCREG_TPC),
                                    shared_data->tpc[i-1]);
                            printRegPair(outs, "Tnpc",
                                    thread->readMiscRegNoEffect(MISCREG_TNPC),
                                    shared_data->tnpc[i-1]);
                            printRegPair(outs, "Tstate",
                                    thread->readMiscRegNoEffect(MISCREG_TSTATE),
                                    shared_data->tstate[i-1]);
                            printRegPair(outs, "Tt",
                                    thread->readMiscRegNoEffect(MISCREG_TT),
                                    shared_data->tt[i-1]);
                            printRegPair(outs, "Htstate",
                                    thread->readMiscRegNoEffect(MISCREG_HTSTATE),
                                    shared_data->htstate[i-1]);
                        }
                        thread->setMiscRegNoEffect(MISCREG_TL, oldTl);
                        outs << endl;

                        printSectionHeader(outs, "General Purpose Registers");
                        static const char * regtypes[4] = {"%g", "%o", "%l", "%i"};
                        for(int y = 0; y < 4; y++) {
                            for(int x = 0; x < 8; x++) {
                                char label[8];
                                sprintf(label, "%s%d", regtypes[y], x);
                                printRegPair(outs, label,
                                        thread->readIntReg(y*8+x),
                                        shared_data->intregs[y*8+x]);
                            }
                        }
                        if (diffFpRegs) {
                            for (int x = 0; x < 32; x++) {
                                char label[8];
                                sprintf(label, "%%f%d", x);
                                printRegPair(outs, label,
                                 thread->readFloatRegBits(x*2,FloatRegFile::DoubleWidth),
                                 shared_data->fpregs[x]);
                            }
                        }
                        if (diffTlb) {
                            printColumnLabels(outs);
                            char label[8];
                            for (int x = 0; x < 64; x++) {
                                if (shared_data->itb[x] !=  ULL(0xFFFFFFFFFFFFFFFF) ||
                                    thread->getITBPtr()->TteRead(x) != ULL(0xFFFFFFFFFFFFFFFF))  {
                                        sprintf(label, "I-TLB:%02d", x);
                                        printRegPair(outs, label, thread->getITBPtr()->TteRead(x),
                                                shared_data->itb[x]);
                                }
                            }
                            for (int x = 0; x < 64; x++) {
                                if (shared_data->dtb[x] !=  ULL(0xFFFFFFFFFFFFFFFF) ||
                                    thread->getDTBPtr()->TteRead(x) != ULL(0xFFFFFFFFFFFFFFFF))  {
                                        sprintf(label, "D-TLB:%02d", x);
                                        printRegPair(outs, label, thread->getDTBPtr()->TteRead(x),
                                                shared_data->dtb[x]);
                                }
                            }
                            thread->getITBPtr()->dumpAll();
                            thread->getDTBPtr()->dumpAll();
                        }

                        diffcount++;
                        if (diffcount > 3)
                            fatal("Differences found between Legion and M5\n");
                    } else
                        diffcount = 0;

                    compared = true;
                    shared_data->flags = OWN_LEGION;
                }
            } // while
        } // if not microop
    }
#endif
}

/* namespace Trace */ }
