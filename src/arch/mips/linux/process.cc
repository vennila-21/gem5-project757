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
 */

#include "arch/mips/linux/linux.hh"
#include "arch/mips/linux/process.hh"
#include "arch/mips/isa_traits.hh"

#include "base/trace.hh"
#include "cpu/exec_context.hh"
#include "kern/linux/linux.hh"

#include "sim/process.hh"
#include "sim/syscall_emul.hh"

using namespace std;
using namespace MipsISA;

/// Target uname() handler.
static SyscallReturn
unameFunc(SyscallDesc *desc, int callnum, Process *process,
          ExecContext *xc)
{
    TypedBufferArg<Linux::utsname> name(xc->getSyscallArg(0));

    strcpy(name->sysname, "Linux");
    strcpy(name->nodename, "m5.eecs.umich.edu");
    strcpy(name->release, "2.4.20");
    strcpy(name->version, "#1 Mon Aug 18 11:32:15 EDT 2003");
    strcpy(name->machine, "mips");

    name.copyOut(xc->getMemPort());
    return 0;
}

/// Target sys_getsysyinfo() handler.  Even though this call is
/// borrowed from Tru64, the subcases that get used appear to be
/// different in practice from those used by Tru64 processes.
static SyscallReturn
sys_getsysinfoFunc(SyscallDesc *desc, int callnum, Process *process,
                   ExecContext *xc)
{
    unsigned op = xc->getSyscallArg(0);
    // unsigned nbytes = xc->getSyscallArg(2);

    switch (op) {

      case 45: { // GSI_IEEE_FP_CONTROL
          TypedBufferArg<uint64_t> fpcr(xc->getSyscallArg(1));
          // I don't think this exactly matches the HW FPCR
          *fpcr = 0;
          fpcr.copyOut(xc->getMemPort());
          return 0;
      }

      default:
        cerr << "sys_getsysinfo: unknown op " << op << endl;
        abort();
        break;
    }

    return 1;
}

/// Target sys_setsysinfo() handler.
static SyscallReturn
sys_setsysinfoFunc(SyscallDesc *desc, int callnum, Process *process,
                   ExecContext *xc)
{
    unsigned op = xc->getSyscallArg(0);
    // unsigned nbytes = xc->getSyscallArg(2);

    switch (op) {

      case 14: { // SSI_IEEE_FP_CONTROL
          TypedBufferArg<uint64_t> fpcr(xc->getSyscallArg(1));
          // I don't think this exactly matches the HW FPCR
          fpcr.copyIn(xc->getMemPort());
          DPRINTFR(SyscallVerbose, "sys_setsysinfo(SSI_IEEE_FP_CONTROL): "
                   " setting FPCR to 0x%x\n", gtoh(*(uint64_t*)fpcr));
          return 0;
      }

      default:
        cerr << "sys_setsysinfo: unknown op " << op << endl;
        abort();
        break;
    }

    return 1;
}


SyscallDesc MipsLinuxProcess::syscallDescs[] = {
    /*  0 */ SyscallDesc("syscall", unimplementedFunc),
    /*  1 */ SyscallDesc("exit", exitFunc),
    /*  2 */ SyscallDesc("fork", unimplementedFunc),
    /*  3 */ SyscallDesc("read", readFunc),
    /*  4 */ SyscallDesc("write", writeFunc),
    /*  5 */ SyscallDesc("open", openFunc<MipsLinux>),
    /*  6 */ SyscallDesc("close", closeFunc),
    /*  7 */ SyscallDesc("waitpid", unimplementedFunc),
    /*  8 */ SyscallDesc("creat", unimplementedFunc),
    /*  9 */ SyscallDesc("link", unimplementedFunc),
    /* 10 */ SyscallDesc("unlink", unlinkFunc),
    /* 11 */ SyscallDesc("execve", unimplementedFunc),
    /* 12 */ SyscallDesc("chdir", unimplementedFunc),
    /* 13 */ SyscallDesc("time", unimplementedFunc),
    /* 14 */ SyscallDesc("mknod", unimplementedFunc),
    /* 15 */ SyscallDesc("chmod", chmodFunc<MipsLinux>),
    /* 16 */ SyscallDesc("lchown", chownFunc),
    /* 17 */ SyscallDesc("break", obreakFunc), /*obreak*/
    /* 18 */ SyscallDesc("unused#18", unimplementedFunc),
    /* 19 */ SyscallDesc("lseek", lseekFunc),
    /* 20 */ SyscallDesc("getpid", getpidFunc),
    /* 21 */ SyscallDesc("mount", unimplementedFunc),
    /* 22 */ SyscallDesc("umount", unimplementedFunc),
    /* 23 */ SyscallDesc("setuid", setuidFunc),
    /* 24 */ SyscallDesc("getuid", getuidFunc),
    /* 25 */ SyscallDesc("stime", unimplementedFunc),
    /* 26 */ SyscallDesc("ptrace", unimplementedFunc),
    /* 27 */ SyscallDesc("alarm", unimplementedFunc),
    /* 28 */ SyscallDesc("unused#28", unimplementedFunc),
    /* 29 */ SyscallDesc("pause", unimplementedFunc),
    /* 30 */ SyscallDesc("utime", unimplementedFunc),
    /* 31 */ SyscallDesc("stty", unimplementedFunc),
    /* 32 */ SyscallDesc("gtty", unimplementedFunc),
    /* 33 */ SyscallDesc("access", unimplementedFunc),
    /* 34 */ SyscallDesc("nice", unimplementedFunc),
    /* 35 */ SyscallDesc("ftime", unimplementedFunc),
    /* 36 */ SyscallDesc("sync", unimplementedFunc),
    /* 37 */ SyscallDesc("kill", ignoreFunc),
    /* 38 */ SyscallDesc("rename", unimplementedFunc),
    /* 39 */ SyscallDesc("mkdir", unimplementedFunc),
    /* 40 */ SyscallDesc("rmdir", unimplementedFunc),
    /* 41 */ SyscallDesc("dup", unimplementedFunc),
    /* 42 */ SyscallDesc("pipe", unimplementedFunc),
    /* 43 */ SyscallDesc("times", unimplementedFunc),
    /* 44 */ SyscallDesc("prof", unimplementedFunc),
    /* 45 */ SyscallDesc("brk", obreakFunc),/*openFunc<MipsLinux>*/
    /* 46 */ SyscallDesc("setgid", unimplementedFunc),
    /* 47 */ SyscallDesc("getgid", getgidFunc),
    /* 48 */ SyscallDesc("signal", ignoreFunc),
    /* 49 */ SyscallDesc("geteuid", geteuidFunc),
    /* 50 */ SyscallDesc("getegid", getegidFunc),
    /* 51 */ SyscallDesc("acct", unimplementedFunc),
    /* 52 */ SyscallDesc("umount2", unimplementedFunc),
    /* 53 */ SyscallDesc("lock", unimplementedFunc),
    /* 54 */ SyscallDesc("ioctl", ioctlFunc<MipsLinux>),
    /* 55 */ SyscallDesc("fcntl", unimplementedFunc),
    /* 56 */ SyscallDesc("mpx", unimplementedFunc),
    /* 57 */ SyscallDesc("setpgid", unimplementedFunc),
    /* 58 */ SyscallDesc("ulimit", unimplementedFunc),
    /* 59 */ SyscallDesc("unused#59", unimplementedFunc),
    /* 60 */ SyscallDesc("umask", unimplementedFunc),
    /* 61 */ SyscallDesc("chroot", unimplementedFunc),
    /* 62 */ SyscallDesc("ustat", unimplementedFunc),
    /* 63 */ SyscallDesc("dup2", unimplementedFunc),
    /* 64 */ SyscallDesc("getppid", getpagesizeFunc),
    /* 65 */ SyscallDesc("getpgrp", unimplementedFunc),
    /* 66 */ SyscallDesc("setsid", unimplementedFunc),
    /* 67 */ SyscallDesc("sigaction",unimplementedFunc),
    /* 68 */ SyscallDesc("sgetmask", unimplementedFunc),
    /* 69 */ SyscallDesc("ssetmask", unimplementedFunc),
    /* 70 */ SyscallDesc("setreuid", unimplementedFunc),
    /* 71 */ SyscallDesc("setregid", unimplementedFunc),
    /* 72 */ SyscallDesc("sigsuspend", unimplementedFunc),
    /* 73 */ SyscallDesc("sigpending", unimplementedFunc),
    /* 74 */ SyscallDesc("sethostname", ignoreFunc),
    /* 75 */ SyscallDesc("setrlimit", unimplementedFunc),
    /* 76 */ SyscallDesc("getrlimit", unimplementedFunc),
    /* 77 */ SyscallDesc("getrusage", unimplementedFunc),
    /* 78 */ SyscallDesc("gettimeofday", unimplementedFunc),
    /* 79 */ SyscallDesc("settimeofday", unimplementedFunc),
    /* 80 */ SyscallDesc("getgroups", unimplementedFunc),
    /* 81 */ SyscallDesc("setgroups", unimplementedFunc),
    /* 82 */ SyscallDesc("reserved#82", unimplementedFunc),
    /* 83 */ SyscallDesc("symlink", unimplementedFunc),
    /* 84 */ SyscallDesc("unused#84", unimplementedFunc),
    /* 85 */ SyscallDesc("readlink", unimplementedFunc),
    /* 86 */ SyscallDesc("uselib", unimplementedFunc),
    /* 87 */ SyscallDesc("swapon", gethostnameFunc),
    /* 88 */ SyscallDesc("reboot", unimplementedFunc),
    /* 89 */ SyscallDesc("readdir", unimplementedFunc),
    /* 90 */ SyscallDesc("mmap", mmapFunc<MipsLinux>),
    /* 91 */ SyscallDesc("munmap",munmapFunc),
    /* 92 */ SyscallDesc("truncate", fcntlFunc),
    /* 93 */ SyscallDesc("ftruncate", unimplementedFunc),
    /* 94 */ SyscallDesc("fchmod", unimplementedFunc),
    /* 95 */ SyscallDesc("fchown", unimplementedFunc),
    /* 96 */ SyscallDesc("getpriority", unimplementedFunc),
    /* 97 */ SyscallDesc("setpriority", unimplementedFunc),
    /* 98 */ SyscallDesc("profil", unimplementedFunc),
    /* 99 */ SyscallDesc("statfs", unimplementedFunc),
    /* 100 */ SyscallDesc("fstatfs", unimplementedFunc),
    /* 101 */ SyscallDesc("ioperm", unimplementedFunc),
    /* 102 */ SyscallDesc("socketcall", unimplementedFunc),
    /* 103 */ SyscallDesc("syslog", unimplementedFunc),
    /* 104 */ SyscallDesc("setitimer", unimplementedFunc),
    /* 105 */ SyscallDesc("getitimer", unimplementedFunc),
    /* 106 */ SyscallDesc("stat",  statFunc<MipsLinux>),
    /* 107 */ SyscallDesc("lstat", unimplementedFunc),
    /* 108 */ SyscallDesc("fstat", fstatFunc<MipsLinux>),
    /* 109 */ SyscallDesc("unused#109", unimplementedFunc),
    /* 110 */ SyscallDesc("iopl", unimplementedFunc),
    /* 111 */ SyscallDesc("vhangup", unimplementedFunc),
    /* 112 */ SyscallDesc("idle", ignoreFunc),
    /* 113 */ SyscallDesc("vm86", unimplementedFunc),
    /* 114 */ SyscallDesc("wait4", unimplementedFunc),
    /* 115 */ SyscallDesc("swapoff", unimplementedFunc),
    /* 116 */ SyscallDesc("sysinfo", unimplementedFunc),
    /* 117 */ SyscallDesc("ipc", unimplementedFunc),
    /* 118 */ SyscallDesc("fsync", unimplementedFunc),
    /* 119 */ SyscallDesc("sigreturn", unimplementedFunc),
    /* 120 */ SyscallDesc("clone", unimplementedFunc),
    /* 121 */ SyscallDesc("setdomainname", unimplementedFunc),
    /* 122 */ SyscallDesc("uname", unameFunc),
    /* 123 */ SyscallDesc("modify_ldt", unimplementedFunc),
    /* 124 */ SyscallDesc("adjtimex", unimplementedFunc),
    /* 125 */ SyscallDesc("mprotect", ignoreFunc),
    /* 126 */ SyscallDesc("sigprocmask", unimplementedFunc),
    /* 127 */ SyscallDesc("create_module", unimplementedFunc),
    /* 128 */ SyscallDesc("init_module", unimplementedFunc),
    /* 129 */ SyscallDesc("delete_module", unimplementedFunc),
    /* 130 */ SyscallDesc("get_kernel_syms", unimplementedFunc),
    /* 131 */ SyscallDesc("quotactl", unimplementedFunc),
    /* 132 */ SyscallDesc("getpgid", unimplementedFunc),
    /* 133 */ SyscallDesc("fchdir", unimplementedFunc),
    /* 134 */ SyscallDesc("bdflush", unimplementedFunc),
    /* 135 */ SyscallDesc("sysfs", unimplementedFunc),
    /* 136 */ SyscallDesc("personality", unimplementedFunc),
    /* 137 */ SyscallDesc("afs_syscall", unimplementedFunc),
    /* 138 */ SyscallDesc("setfsuid", unimplementedFunc),
    /* 139 */ SyscallDesc("setfsgid", unimplementedFunc),
    /* 140 */ SyscallDesc("llseek", unimplementedFunc),
    /* 141 */ SyscallDesc("getdents", unimplementedFunc),
    /* 142 */ SyscallDesc("newselect", unimplementedFunc),
    /* 143 */ SyscallDesc("flock", unimplementedFunc),
    /* 144 */ SyscallDesc("msync", unimplementedFunc),/*getrlimitFunc<MipsLinux>*/
    /* 145 */ SyscallDesc("readv", unimplementedFunc),
    /* 146 */ SyscallDesc("writev", writevFunc<MipsLinux>),
    /* 147 */ SyscallDesc("cacheflush", unimplementedFunc),
    /* 148 */ SyscallDesc("cachectl", unimplementedFunc),
    /* 149 */ SyscallDesc("sysmips", unimplementedFunc),
    /* 150 */ SyscallDesc("unused#150", unimplementedFunc),
    /* 151 */ SyscallDesc("getsid", unimplementedFunc),
    /* 152 */ SyscallDesc("fdatasync", unimplementedFunc),
    /* 153 */ SyscallDesc("sysctl", unimplementedFunc),
    /* 154 */ SyscallDesc("mlock", unimplementedFunc),
    /* 155 */ SyscallDesc("munlock", unimplementedFunc),
    /* 156 */ SyscallDesc("mlockall", unimplementedFunc),
    /* 157 */ SyscallDesc("munlockall", unimplementedFunc),
    /* 158 */ SyscallDesc("sched_setparam", unimplementedFunc),
    /* 159 */ SyscallDesc("sched_getparam", unimplementedFunc),
    /* 160 */ SyscallDesc("sched_setscheduler", unimplementedFunc),
    /* 161 */ SyscallDesc("sched_getscheduler", unimplementedFunc),
    /* 162 */ SyscallDesc("sched_yield", unimplementedFunc),
    /* 163 */ SyscallDesc("sched_get_prioritymax", unimplementedFunc),
    /* 164 */ SyscallDesc("sched_get_priority_min", unimplementedFunc),
    /* 165 */ SyscallDesc("sched_rr_get_interval", unimplementedFunc),
    /* 166 */ SyscallDesc("nanosleep", unimplementedFunc),
    /* 167 */ SyscallDesc("mremap", unimplementedFunc),
    /* 168 */ SyscallDesc("accept", unimplementedFunc),
    /* 169 */ SyscallDesc("bind", unimplementedFunc),
    /* 170 */ SyscallDesc("connect", unimplementedFunc),
    /* 171 */ SyscallDesc("getpeername", unimplementedFunc),
    /* 172 */ SyscallDesc("getsockname", unimplementedFunc),
    /* 173 */ SyscallDesc("getsockopt", unimplementedFunc),
    /* 174 */ SyscallDesc("listen", unimplementedFunc),
    /* 175 */ SyscallDesc("recv", unimplementedFunc),
    /* 176 */ SyscallDesc("recvmsg", unimplementedFunc),
    /* 177 */ SyscallDesc("send", unimplementedFunc),
    /* 178 */ SyscallDesc("sendmsg", ignoreFunc),
    /* 179 */ SyscallDesc("sendto", unimplementedFunc),
    /* 180 */ SyscallDesc("setsockopt", unimplementedFunc),
    /* 181 */ SyscallDesc("shutdown", unimplementedFunc),
    /* 182 */ SyscallDesc("unknown #182", unimplementedFunc),
    /* 183 */ SyscallDesc("socket", ignoreFunc),
    /* 184 */ SyscallDesc("socketpair", unimplementedFunc),
    /* 185 */ SyscallDesc("setresuid", unimplementedFunc),
    /* 186 */ SyscallDesc("getresuid", unimplementedFunc),
    /* 187 */ SyscallDesc("query_module", unimplementedFunc),
    /* 188 */ SyscallDesc("poll", unimplementedFunc),
    /* 189 */ SyscallDesc("nfsservctl", unimplementedFunc),
    /* 190 */ SyscallDesc("setresgid", unimplementedFunc),
    /* 191 */ SyscallDesc("getresgid", unimplementedFunc),
    /* 192 */ SyscallDesc("prctl", unimplementedFunc),
    /* 193 */ SyscallDesc("rt_sigreturn", unimplementedFunc),
    /* 194 */ SyscallDesc("rt_sigaction", ignoreFunc),
    /* 195 */ SyscallDesc("rt_sigprocmask", ignoreFunc),
    /* 196 */ SyscallDesc("rt_sigpending", unimplementedFunc),
    /* 197 */ SyscallDesc("rt_sigtimedwait", unimplementedFunc),
    /* 198 */ SyscallDesc("rt_sigqueueinfo", ignoreFunc),
    /* 199 */ SyscallDesc("rt_sigsuspend", unimplementedFunc),
    /* 200 */ SyscallDesc("pread64", unimplementedFunc),
    /* 201 */ SyscallDesc("pwrite64", unimplementedFunc),
    /* 202 */ SyscallDesc("chown", unimplementedFunc),
    /* 203 */ SyscallDesc("getcwd", unimplementedFunc),
    /* 204 */ SyscallDesc("capget", unimplementedFunc),
    /* 205 */ SyscallDesc("capset", unimplementedFunc),
    /* 206 */ SyscallDesc("sigalstack", unimplementedFunc),
    /* 207 */ SyscallDesc("sendfile", unimplementedFunc),
    /* 208 */ SyscallDesc("getpmsg", unimplementedFunc),
    /* 209 */ SyscallDesc("putpmsg", unimplementedFunc),
    /* 210 */ SyscallDesc("mmap2", unimplementedFunc),
    /* 211 */ SyscallDesc("truncate64", unimplementedFunc),
    /* 212 */ SyscallDesc("ftruncate64", unimplementedFunc),
    /* 213 */ SyscallDesc("stat64", unimplementedFunc),
    /* 214 */ SyscallDesc("lstat64", lstat64Func<MipsLinux>),
    /* 215 */ SyscallDesc("fstat64", fstat64Func<MipsLinux>),
    /* 216 */ SyscallDesc("pivot_root", unimplementedFunc),
    /* 217 */ SyscallDesc("mincore", unimplementedFunc),
    /* 218 */ SyscallDesc("madvise", unimplementedFunc),
    /* 219 */ SyscallDesc("getdents64", unimplementedFunc),
    /* 220 */ SyscallDesc("fcntl64", fcntlFunc),
    /* 221 */ SyscallDesc("reserved#221", unimplementedFunc),
    /* 222 */ SyscallDesc("gettid", unimplementedFunc),
    /* 223 */ SyscallDesc("readahead", unimplementedFunc),
    /* 224 */ SyscallDesc("setxattr", unimplementedFunc),
    /* 225 */ SyscallDesc("lsetxattr", unimplementedFunc),
    /* 226 */ SyscallDesc("fsetxattr", unimplementedFunc),
    /* 227 */ SyscallDesc("getxattr", unimplementedFunc),
    /* 228 */ SyscallDesc("lgetxattr", unimplementedFunc),
    /* 229 */ SyscallDesc("fgetxattr", unimplementedFunc),
    /* 230 */ SyscallDesc("listxattr", unimplementedFunc),
    /* 231 */ SyscallDesc("llistxattr", unimplementedFunc),
    /* 232 */ SyscallDesc("flistxattr", unimplementedFunc),
    /* 233 */ SyscallDesc("removexattr", unimplementedFunc),
    /* 234 */ SyscallDesc("lremovexattr", unimplementedFunc),
    /* 235 */ SyscallDesc("fremovexattr", ignoreFunc),
    /* 236 */ SyscallDesc("tkill", unimplementedFunc),
    /* 237 */ SyscallDesc("sendfile64", unimplementedFunc),
    /* 238 */ SyscallDesc("futex", unimplementedFunc),
    /* 239 */ SyscallDesc("sched_setaffinity", unimplementedFunc),
    /* 240 */ SyscallDesc("sched_getaffinity", unimplementedFunc),
    /* 241 */ SyscallDesc("io_setup", unimplementedFunc),
    /* 242 */ SyscallDesc("io_destroy", unimplementedFunc),
    /* 243 */ SyscallDesc("io_getevents", unimplementedFunc),
    /* 244 */ SyscallDesc("io_submit", unimplementedFunc),
    /* 245 */ SyscallDesc("io_cancel", unimplementedFunc),
    /* 246 */ SyscallDesc("exit_group", exitFunc),
    /* 247 */ SyscallDesc("lookup_dcookie", unimplementedFunc),
    /* 248 */ SyscallDesc("epoll_create", unimplementedFunc),
    /* 249 */ SyscallDesc("epoll_ctl", unimplementedFunc),
    /* 250 */ SyscallDesc("epoll_wait", unimplementedFunc),
    /* 251 */ SyscallDesc("remap_file_pages", unimplementedFunc),
    /* 252 */ SyscallDesc("set_tid_address", unimplementedFunc),
    /* 253 */ SyscallDesc("restart_syscall", unimplementedFunc),
    /* 254 */ SyscallDesc("fadvise64", unimplementedFunc),
    /* 255 */ SyscallDesc("statfs64", unimplementedFunc),
    /* 256 */ SyscallDesc("fstafs64", unimplementedFunc),
    /* 257 */ SyscallDesc("timer_create", sys_getsysinfoFunc),
    /* 258 */ SyscallDesc("timer_settime", sys_setsysinfoFunc),
    /* 259 */ SyscallDesc("timer_gettime", unimplementedFunc),
    /* 260 */ SyscallDesc("timer_getoverrun", unimplementedFunc),
    /* 261 */ SyscallDesc("timer_delete", unimplementedFunc),
    /* 262 */ SyscallDesc("clock_settime", unimplementedFunc),
    /* 263 */ SyscallDesc("clock_gettime", unimplementedFunc),
    /* 264 */ SyscallDesc("clock_getres", unimplementedFunc),
    /* 265 */ SyscallDesc("clock_nanosleep", unimplementedFunc),
    /* 266 */ SyscallDesc("tgkill", unimplementedFunc),
    /* 267 */ SyscallDesc("utimes", unimplementedFunc),
    /* 268 */ SyscallDesc("mbind", unimplementedFunc),
    /* 269 */ SyscallDesc("get_mempolicy", unimplementedFunc),
    /* 270 */ SyscallDesc("set_mempolicy", unimplementedFunc),
    /* 271 */ SyscallDesc("mq_open", unimplementedFunc),
    /* 272 */ SyscallDesc("mq_unlink", unimplementedFunc),
    /* 273 */ SyscallDesc("mq_timedsend", unimplementedFunc),
    /* 274 */ SyscallDesc("mq_timedreceive", unimplementedFunc),
    /* 275 */ SyscallDesc("mq_notify", unimplementedFunc),
    /* 276 */ SyscallDesc("mq_getsetattr", unimplementedFunc),
    /* 277 */ SyscallDesc("vserver", unimplementedFunc),
    /* 278 */ SyscallDesc("waitid", unimplementedFunc),
    /* 279 */ SyscallDesc("unknown #279", unimplementedFunc),
    /* 280 */ SyscallDesc("add_key", unimplementedFunc),
    /* 281 */ SyscallDesc("request_key", unimplementedFunc),
    /* 282 */ SyscallDesc("keyctl", unimplementedFunc),
};

MipsLinuxProcess::MipsLinuxProcess(const std::string &name,
                                     ObjectFile *objFile,
                                     System *system,
                                     int stdin_fd,
                                     int stdout_fd,
                                     int stderr_fd,
                                     std::vector<std::string> &argv,
                                     std::vector<std::string> &envp)
    : MipsLiveProcess(name, objFile, system, stdin_fd, stdout_fd, stderr_fd,
                      argv, envp),
     Num_Syscall_Descs(sizeof(syscallDescs) / sizeof(SyscallDesc))
{
    //init_regs->intRegFile[0] = 0;
}

SyscallDesc*
MipsLinuxProcess::getDesc(int callnum)
{
    //MIPS32 syscalls are in the range of 4000 - 4999
    int m5_sys_idx = callnum - 4000;

    if (m5_sys_idx < 0 || m5_sys_idx > Num_Syscall_Descs)
        return NULL;

    return &syscallDescs[m5_sys_idx];
}
