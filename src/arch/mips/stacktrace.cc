/*
 * Copyright .AN) 2007 MIPS Technologies, Inc.  All Rights Reserved
 *
 * This software is part of the M5 simulator.
 *
 * THIS IS A LEGAL AGREEMENT.  BY DOWNLOADING, USING, COPYING, CREATING
 * DERIVATIVE WORKS, AND/OR DISTRIBUTING THIS SOFTWARE YOU ARE AGREEING
 * TO THESE TERMS AND CONDITIONS.
 *
 * Permission is granted to use, copy, create derivative works and
 * distribute this software and such derivative works for any purpose,
 * so long as (1) the copyright notice above, this grant of permission,
 * and the disclaimer below appear in all copies and derivative works
 * made, (2) the copyright notice above is augmented as appropriate to
 * reflect the addition of any new copyrightable work in a derivative
 * work (e.g., Copyright .AN) <Publication Year> Copyright Owner), and (3)
 * the name of MIPS Technologies, Inc. ($B!H(BMIPS$B!I(B) is not used in any
 * advertising or publicity pertaining to the use or distribution of
 * this software without specific, written prior authorization.
 *
 * THIS SOFTWARE IS PROVIDED $B!H(BAS IS.$B!I(B  MIPS MAKES NO WARRANTIES AND
 * DISCLAIMS ALL WARRANTIES, WHETHER EXPRESS, STATUTORY, IMPLIED OR
 * OTHERWISE, INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
 * NON-INFRINGEMENT OF THIRD PARTY RIGHTS, REGARDING THIS SOFTWARE.
 * IN NO EVENT SHALL MIPS BE LIABLE FOR ANY DAMAGES, INCLUDING DIRECT,
 * INDIRECT, INCIDENTAL, CONSEQUENTIAL, SPECIAL, OR PUNITIVE DAMAGES OF
 * ANY KIND OR NATURE, ARISING OUT OF OR IN CONNECTION WITH THIS AGREEMENT,
 * THIS SOFTWARE AND/OR THE USE OF THIS SOFTWARE, WHETHER SUCH LIABILITY
 * IS ASSERTED ON THE BASIS OF CONTRACT, TORT (INCLUDING NEGLIGENCE OR
 * STRICT LIABILITY), OR OTHERWISE, EVEN IF MIPS HAS BEEN WARNED OF THE
 * POSSIBILITY OF ANY SUCH LOSS OR DAMAGE IN ADVANCE.
 *
 * Authors: Nathan L. Binkert
 */

#include <string>

#include "arch/mips/isa_traits.hh"
#include "arch/mips/stacktrace.hh"
#include "arch/mips/vtophys.hh"
#include "base/bitfield.hh"
#include "base/trace.hh"
#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "sim/system.hh"

using namespace std;
using namespace MipsISA;

ProcessInfo::ProcessInfo(ThreadContext *_tc)
    : tc(_tc)
{
//     Addr addr = 0;

    VirtualPort *vp;

    vp = tc->getVirtPort();

//     if (!tc->getSystemPtr()->kernelSymtab->findAddress("thread_info_size", addr))
//         panic("thread info not compiled into kernel\n");
//     thread_info_size = vp->readGtoH<int32_t>(addr);

//     if (!tc->getSystemPtr()->kernelSymtab->findAddress("task_struct_size", addr))
//         panic("thread info not compiled into kernel\n");
//     task_struct_size = vp->readGtoH<int32_t>(addr);

//     if (!tc->getSystemPtr()->kernelSymtab->findAddress("thread_info_task", addr))
//         panic("thread info not compiled into kernel\n");
//     task_off = vp->readGtoH<int32_t>(addr);

//     if (!tc->getSystemPtr()->kernelSymtab->findAddress("task_struct_pid", addr))
//         panic("thread info not compiled into kernel\n");
//     pid_off = vp->readGtoH<int32_t>(addr);

//     if (!tc->getSystemPtr()->kernelSymtab->findAddress("task_struct_comm", addr))
//         panic("thread info not compiled into kernel\n");
//     name_off = vp->readGtoH<int32_t>(addr);

    tc->delVirtPort(vp);
}

Addr
ProcessInfo::task(Addr ksp) const
{
    Addr base = ksp & ~0x3fff;
    if (base == ULL(0xfffffc0000000000))
        return 0;

    Addr tsk;

    VirtualPort *vp;

    vp = tc->getVirtPort();
    tsk = vp->readGtoH<Addr>(base + task_off);
    tc->delVirtPort(vp);

    return tsk;
}

int
ProcessInfo::pid(Addr ksp) const
{
    Addr task = this->task(ksp);
    if (!task)
        return -1;

    uint16_t pd;

    VirtualPort *vp;

    vp = tc->getVirtPort();
    pd = vp->readGtoH<uint16_t>(task + pid_off);
    tc->delVirtPort(vp);

    return pd;
}

string
ProcessInfo::name(Addr ksp) const
{
    Addr task = this->task(ksp);
    if (!task)
        return "console";

    char comm[256];
    CopyStringOut(tc, comm, task + name_off, sizeof(comm));
    if (!comm[0])
        return "startup";

    return comm;
}

StackTrace::StackTrace()
    : tc(0), stack(64)
{
}

StackTrace::StackTrace(ThreadContext *_tc, StaticInstPtr inst)
    : tc(0), stack(64)
{
    trace(_tc, inst);
}

StackTrace::~StackTrace()
{
}

void
StackTrace::trace(ThreadContext *_tc, bool is_call)
{
    tc = _tc;
    /* FIXME - Jaidev - What is IPR_DTB_CM in Alpha? */
    bool usermode = 0;
      //(tc->readMiscReg(MipsISA::IPR_DTB_CM) & 0x18) != 0;

//     Addr pc = tc->readNextPC();
//     bool kernel = tc->getSystemPtr()->kernelStart <= pc &&
//         pc <= tc->getSystemPtr()->kernelEnd;

    if (usermode) {
        stack.push_back(user);
        return;
    }

//     if (!kernel) {
//         stack.push_back(console);
//         return;
//     }

//     SymbolTable *symtab = tc->getSystemPtr()->kernelSymtab;
//     Addr ksp = tc->readIntReg(TheISA::StackPointerReg);
//     Addr bottom = ksp & ~0x3fff;
//     Addr addr;

//     if (is_call) {
//         if (!symtab->findNearestAddr(pc, addr))
//             panic("could not find address %#x", pc);

//         stack.push_back(addr);
//         pc = tc->readPC();
//     }

//     Addr ra;
//     int size;

//     while (ksp > bottom) {
//         if (!symtab->findNearestAddr(pc, addr))
//             panic("could not find symbol for pc=%#x", pc);
//         assert(pc >= addr && "symbol botch: callpc < func");

//         stack.push_back(addr);

//         if (isEntry(addr))
//             return;

//         if (decodePrologue(ksp, pc, addr, size, ra)) {
//             if (!ra)
//                 return;

//             if (size <= 0) {
//                 stack.push_back(unknown);
//                 return;
//             }

//             pc = ra;
//             ksp += size;
//         } else {
//             stack.push_back(unknown);
//             return;
//         }

//         bool kernel = tc->getSystemPtr()->kernelStart <= pc &&
//             pc <= tc->getSystemPtr()->kernelEnd;
//         if (!kernel)
//             return;

//         if (stack.size() >= 1000)
//             panic("unwinding too far");
//     }

//     panic("unwinding too far");
}

bool
StackTrace::isEntry(Addr addr)
{
  /*    if (addr == tc->readMiscReg(MipsISA::IPR_PALtemp2))
        return true;*/

    return false;
}

bool
StackTrace::decodeStack(MachInst inst, int &disp)
{
    // lda $sp, -disp($sp)
    //
    // Opcode<31:26> == 0x08
    // RA<25:21> == 30
    // RB<20:16> == 30
    // Disp<15:0>
    const MachInst mem_mask = 0xffff0000;
    const MachInst lda_pattern = 0x23de0000;
    const MachInst lda_disp_mask = 0x0000ffff;

    // subq $sp, disp, $sp
    // addq $sp, disp, $sp
    //
    // Opcode<31:26> == 0x10
    // RA<25:21> == 30
    // Lit<20:13>
    // One<12> = 1
    // Func<11:5> == 0x20 (addq)
    // Func<11:5> == 0x29 (subq)
    // RC<4:0> == 30
    const MachInst intop_mask = 0xffe01fff;
    const MachInst addq_pattern = 0x43c0141e;
    const MachInst subq_pattern = 0x43c0153e;
    const MachInst intop_disp_mask = 0x001fe000;
    const int intop_disp_shift = 13;

    if ((inst & mem_mask) == lda_pattern)
        disp = -sext<16>(inst & lda_disp_mask);
    else if ((inst & intop_mask) == addq_pattern)
        disp = -int((inst & intop_disp_mask) >> intop_disp_shift);
    else if ((inst & intop_mask) == subq_pattern)
        disp = int((inst & intop_disp_mask) >> intop_disp_shift);
    else
        return false;

    return true;
}

bool
StackTrace::decodeSave(MachInst inst, int &reg, int &disp)
{
    // lda $stq, disp($sp)
    //
    // Opcode<31:26> == 0x08
    // RA<25:21> == ?
    // RB<20:16> == 30
    // Disp<15:0>
    const MachInst stq_mask = 0xfc1f0000;
    const MachInst stq_pattern = 0xb41e0000;
    const MachInst stq_disp_mask = 0x0000ffff;
    const MachInst reg_mask = 0x03e00000;
    const int reg_shift = 21;

    if ((inst & stq_mask) == stq_pattern) {
        reg = (inst & reg_mask) >> reg_shift;
        disp = sext<16>(inst & stq_disp_mask);
    } else {
        return false;
    }

    return true;
}

/*
 * Decode the function prologue for the function we're in, and note
 * which registers are stored where, and how large the stack frame is.
 */
bool
StackTrace::decodePrologue(Addr sp, Addr callpc, Addr func,
                           int &size, Addr &ra)
{
    size = 0;
    ra = 0;

    for (Addr pc = func; pc < callpc; pc += sizeof(MachInst)) {
        MachInst inst;
        CopyOut(tc, (uint8_t *)&inst, pc, sizeof(MachInst));

        int reg, disp;
        if (decodeStack(inst, disp)) {
            if (size) {
                // panic("decoding frame size again");
                return true;
            }
            size += disp;
        } else if (decodeSave(inst, reg, disp)) {
            if (!ra && reg == ReturnAddressReg) {
                CopyOut(tc, (uint8_t *)&ra, sp + disp, sizeof(Addr));
                if (!ra) {
                    // panic("no return address value pc=%#x\n", pc);
                    return false;
                }
            }
        }
    }

    return true;
}

#if TRACING_ON
void
StackTrace::dump()
{
    StringWrap name(tc->getCpuPtr()->name());
//     SymbolTable *symtab = tc->getSystemPtr()->kernelSymtab;

    DPRINTFN("------ Stack ------\n");

//     string symbol;
//     for (int i = 0, size = stack.size(); i < size; ++i) {
//         Addr addr = stack[size - i - 1];
//         if (addr == user)
//             symbol = "user";
//         else if (addr == console)
//             symbol = "console";
//         else if (addr == unknown)
//             symbol = "unknown";
//         else
//             symtab->findSymbol(addr, symbol);

//         DPRINTFN("%#x: %s\n", addr, symbol);
//     }
}
#endif
