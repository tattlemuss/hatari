#include "tos.h"
#include "../hopper68/instruction68.h"
#include "../models/memory.h"

const char* GetGemdosName(uint16_t id)
{
    switch (id)
    {
        case 0x00 	: return "Pterm0";
        case 0x01 	: return "Cconin";
        case 0x02 	: return "Cconout";
        case 0x03 	: return "Cauxin";
        case 0x04 	: return "Cauxout";
        case 0x05 	: return "Cprnout";
        case 0x06 	: return "Crawio";
        case 0x07 	: return "Crawcin";
        case 0x08 	: return "Cnecin";
        case 0x09 	: return "Cconws";
        case 0x0A 	: return "Cconrs";
        case 0x0B 	: return "Cconis";
        case 0x0E 	: return "Dsetdrv";
        case 0x10 	: return "Cconos";
        case 0x11 	: return "Cprnos";
        case 0x12 	: return "Cauxis";
        case 0x13 	: return "Cauxos";
        case 0x14 	: return "Maddalt";
        case 0x15 	: return "Srealloc 4";
        case 0x16 	: return "Slbopen 	MagiC";
        case 0x17 	: return "Slbclose 	MagiC";
        case 0x19 	: return "Dgetdrv";
        case 0x1A 	: return "Fsetdta";
        case 0x20 	: return "Super";
        case 0x2A 	: return "Tgetdate";
        case 0x2B 	: return "Tsetdate";
        case 0x2C 	: return "Tgettime";
        case 0x2D 	: return "Tsettime";
        case 0x2F 	: return "Fgetdta";
        case 0x30 	: return "Sversion";
        case 0x31 	: return "Ptermres";
        case 0x33 	: return "Sconfig 	MagiC";
        case 0x36 	: return "Dfree";
        case 0x39 	: return "Dcreate";
        case 0x3A 	: return "Ddelete";
        case 0x3B 	: return "Dsetpath";
        case 0x3C 	: return "Fcreate";
        case 0x3D 	: return "Fopen";
        case 0x3E 	: return "Fclose";
        case 0x3F 	: return "Fread";
        case 0x40 	: return "Fwrite";
        case 0x41 	: return "Fdelete";
        case 0x42 	: return "Fseek";
        case 0x43 	: return "Fattrib";
        case 0x44 	: return "Mxalloc";
        case 0x45 	: return "Fdup";
        case 0x46 	: return "Fforce";
        case 0x47 	: return "Dgetpath";
        case 0x48 	: return "Malloc";
        case 0x49 	: return "Mfree";
        case 0x4A 	: return "Mshrink";
        case 0x4B 	: return "Pexec";
        case 0x4C 	: return "Pterm";
        case 0x4E 	: return "Fsfirst";
        case 0x4F 	: return "Fsnext";
        case 0x56 	: return "Frename";
        case 0x57 	: return "Fdatime";
        case 0x5C 	: return "Flock 	";
        case 0x60 	: return "Nversion 	";
        case 0x62 	: return "Frlock 	";
        case 0x63 	: return "Frunlock 	";
        case 0x64 	: return "F_lock 	";
        case 0x65 	: return "Funlock 	";
        case 0x66 	: return "Fflush 	";
        case 0xFF 	: return "Syield 	MiNT";
        case 0x100 	: return "Fpipe 	MiNT";
        case 0x101 	: return "Ffchown 	MiNT";
        case 0x102 	: return "Ffchmod 	MiNT";
        case 0x103 	: return "Fsync 	MiNT, no implemented yet";
        case 0x104 	: return "Fcntl 	MiNT";
        case 0x105 	: return "Finstat 	MiNT";
        case 0x106 	: return "Foutstat 	MiNT";
        case 0x107 	: return "Fgetchar 	MiNT";
        case 0x108 	: return "Fputchar 	MiNT";
        case 0x109 	: return "Pwait 	MiNT";
        case 0x10A 	: return "Pnice 	MiNT";
        case 0x10B 	: return "Pgetpid 	MiNT";
        case 0x10C 	: return "Pgetppid 	MiNT";
        case 0x10D 	: return "Pgetpgrp 	MiNT";
        case 0x10E 	: return "Psetpgrp 	MiNT";
        case 0x10F 	: return "Pgetuid 	MiNT";
        case 0x110 	: return "Psetuid 	MiNT";
        case 0x111 	: return "Pkill 	MiNT";
        case 0x112 	: return "Psignal 	MiNT";
        case 0x113 	: return "Pvfork 	MiNT";
        case 0x114 	: return "Pgetgid 	MiNT";
        case 0x115 	: return "Psetgid 	MiNT";
        case 0x116 	: return "Psigblock 	MiNT";
        case 0x117 	: return "Psigsetmask 	MiNT";
        case 0x118 	: return "Pusrval 	MiNT";
        case 0x119 	: return "Pdomain 	MiNT";
        case 0x11A 	: return "Psigreturn 	MiNT";
        case 0x11B 	: return "Pfork 	MiNT";
        case 0x11C 	: return "Pwait3 	MiNT";
        case 0x11D 	: return "Fselect 	MiNT";
        case 0x11E 	: return "Prusage 	MiNT";
        case 0x11F 	: return "Psetlimit 	MiNT";
        case 0x120 	: return "Talarm 	MiNT";
        case 0x121 	: return "Pause 	MiNT";
        case 0x122 	: return "Sysconf 	MiNT";
        case 0x123 	: return "Psigpending 	MiNT";
        case 0x124 	: return "Dpathconf 	MiNT";
        case 0x125 	: return "Pmsg 	MiNT";
        case 0x126 	: return "Fmidipipe 	MiNT";
        case 0x127 	: return "Prenice 	MiNT";
        case 0x128 	: return "Dopendir 	MiNT";
        case 0x129 	: return "Dreaddir 	MiNT";
        case 0x12A 	: return "Drewinddir 	MiNT";
        case 0x12B 	: return "Dclosedir 	MiNT";
        case 0x12C 	: return "Fxattr 	MiNT";
        case 0x12D 	: return "Flink 	MiNT";
        case 0x12E 	: return "Fsymlink 	MiNT";
        case 0x12F 	: return "Freadlink 	MiNT";
        case 0x130 	: return "Dcntl 	MiNT";
        case 0x131 	: return "Fchown 	MiNT";
        case 0x132 	: return "Fchmod 	MiNT";
        case 0x133 	: return "Pumask 	MiNT";
        case 0x134 	: return "Psemaphore 	MiNT";
        case 0x135 	: return "Dlock 	MiNT";
        case 0x136 	: return "Psigpause 	MiNT";
        case 0x137 	: return "Psigaction 	MiNT";
        case 0x138 	: return "Pgeteuid 	MiNT";
        case 0x139 	: return "Pgetegid 	MiNT";
        case 0x13A 	: return "Pwaitpid 	MiNT";
        case 0x13B 	: return "Dgetcwd 	MiNT";
        case 0x13C 	: return "Salert 	MiNT";
        case 0x13D 	: return "Tmalarm 	MiNT 1.10";
        case 0x13E 	: return "Psigintr 	MiNT 1.11 until FreeMiNT 1.15.12 inclusive";
        case 0x13F 	: return "Suptime 	MiNT 1.11";
        case 0x140 	: return "Ptrace 	MiNT";
        case 0x141 	: return "Mvalidate 	MiNT";
        case 0x142 	: return "Dxreaddir 	MiNT 1.11";
        case 0x143 	: return "Pseteuid 	MiNT 1.11";
        case 0x144 	: return "Psetegid 	MiNT 1.11";
        case 0x145 	: return "Pgetauid 	MiNT 1.11";
        case 0x146 	: return "Psetauid 	MiNT 1.11";
        case 0x147 	: return "Pgetgroups 	MiNT 1.11";
        case 0x148 	: return "Psetgroups 	MiNT 1.11";
        case 0x149 	: return "Tsetitimer 	MiNT 1.11";
        case 0x14A 	: return "Scookie 	MiNT (obsolete)";
        case 0x14B 	: return "Fstat64 	MiNT";
        case 0x14C 	: return "Fseek64 	MiNT";
        case 0x14D 	: return "Dsetkey 	MiNT";
        case 0x14E 	: return "Psetreuid 	MiNT 1.12";
        case 0x14F 	: return "Psetregid 	MiNT 1.12";
        case 0x150 	: return "Ssync 	MiNT, MagiC";
        case 0x151 	: return "Shutdown 	MiNT";
        case 0x152 	: return "Dreadlabel 	MiNT 1.12";
        case 0x153 	: return "Dwritelabel 	MiNT 1.12";
        case 0x154 	: return "Ssystem 	MiNT 1.15.0";
        case 0x155 	: return "Tgettimeofday 	MiNT 1.15.0";
        case 0x156 	: return "Tsettimeofday 	MiNT 1.15.0";
        case 0x157 	: return "Tadjtime 	MiNT, no implemented yet";
        case 0x158 	: return "Pgetpriority 	MiNT 1.15.0";
        case 0x159 	: return "Psetpriority 	MiNT 1.15.0";
        case 0x15a 	: return "Fpoll 	MiNTNet";
        case 0x15B 	: return "Fwritev 	MiNTNet";
        case 0x15C 	: return "Freadv 	MiNTNet";
        case 0x15D 	: return "Ffstat64 	MiNTNet";
        case 0x15E 	: return "Psysctl 	MiNT";
        case 0x15F 	: return "Pemulation 	MiNT";
        case 0x160 	: return "Fsocket 	MiNTNet";
        case 0x161 	: return "Fsocketpair 	MiNTNet";
        case 0x162 	: return "Faccept 	MiNTNet";
        case 0x163 	: return "Fconnect 	MiNTNet";
        case 0x164 	: return "Fbind 	MiNTNet";
        case 0x165 	: return "Flisten 	MiNTNet";
        case 0x166 	: return "Frecvmsg 	MiNTNet";
        case 0x167 	: return "Fsendmsg 	MiNTNet";
        case 0x168 	: return "Frecvfrom 	MiNTNet";
        case 0x169 	: return "Fsendto 	MiNTNet";
        case 0x16A 	: return "Fsetsockopt 	MiNTNet";
        case 0x16B 	: return "Fgetsockopt 	MiNTNet";
        case 0x16C 	: return "Fgetpeername 	MiNTNet";
        case 0x16D 	: return "Fgetsockname 	MiNTNet";
        case 0x16E 	: return "Fshutdown 	MiNTNet";
        case 0x170 	: return "Pshmget 	MiNT";
        case 0x171 	: return "Pshmctl 	MiNT";
        case 0x172 	: return "Pshmat 	MiNT";
        case 0x173 	: return "Pshmdt 	MiNT";
        case 0x174 	: return "Psemget 	MiNT";
        case 0x175 	: return "Psemctl 	MiNT";
        case 0x176 	: return "Psemop 	MiNT";
        case 0x177 	: return "Psemconfig 	MiNT";
        case 0x178 	: return "Pmsgget 	MiNT";
        case 0x179 	: return "Pmsgctl 	MiNT";
        case 0x17A 	: return "Pmsgsnd 	MiNT";
        case 0x17B 	: return "Pmsgrcv 	MiNT";
        case 0x17D 	: return "Maccess 	MiNT";
        case 0x180 	: return "Fchown16 	FreeMiNT 1.16.0";
        case 0x181 	: return "Fchdir 	FreeMiNT 1.17";
        case 0x182 	: return "Ffdopendir 	FreeMiNT 1.17";
        case 0x183 	: return "Fdirfd 	FreeMiNT 1.17";
        case 0x1068 : return "ys_Break 	SysMon";
        case 0x1069 : return "ys_Break 	SysMon";
        case 0x5DC0 : return "TEFcntrl 	STEmulator";
    }
    return "Unknown";
}

const char* GetBiosName(uint16_t id)
{
    switch (id)
    {
    case 0x00 : return "Getmpb";
    case 0x01 : return "Bconstat";
    case 0x02 : return "Bconin";
    case 0x03 : return "Bconout";
    case 0x04 : return "Rwabs";
    case 0x05 : return "Setexc";
    case 0x06 : return "Tickcal";
    case 0x07 : return "Getbpb";
    case 0x08 : return "Bcostat";
    case 0x09 : return "Mediach";
    case 0x0A : return "Drvmap";
    case 0x0B : return "Kbshift";
    }
    return "Unknown";
}

const char* GetXbiosName(uint16_t id)
{
    switch (id)
    {
        case 0x00 :	return "Initmouse";
        case 0x01 :	return "Ssbrk";
        case 0x02 :	return "Physbase";
        case 0x03 :	return "Logbase";
        case 0x04 :	return "Getrez";
        case 0x05 :	return "Setscreen/VSetscreen";
        case 0x06 :	return "Setpalette";
        case 0x07 :	return "Setcolor";
        case 0x08 :	return "Floprd";
        case 0x09 :	return "Flopwr";
        case 0x0A :	return "Flopfmt";
        case 0x0B :	return "Dbmsg 	Atari Debugger";
        case 0x0C :	return "Midiws";
        case 0x0D :	return "Mfpint";
        case 0x0E :	return "Iorec";
        case 0x0F :	return "Rsconf";
        case 0x10 :	return "Keytbl";
        case 0x11 :	return "Random";
        case 0x12 :	return "Protobt";
        case 0x13 :	return "Flopver";
        case 0x14 :	return "Scrdmp";
        case 0x15 :	return "Cursconf";
        case 0x16 :	return "Settime";
        case 0x17 :	return "Gettime";
        case 0x18 :	return "Bioskeys";
        case 0x19 :	return "Ikbdws";
        case 0x1A :	return "Jdisint";
        case 0x1B :	return "Jenabint";
        case 0x1C :	return "Giaccess";
        case 0x1D :	return "Offgibit";
        case 0x1E :	return "Ongibit";
        case 0x1F :	return "Xbtimer";
        case 0x20 :	return "Dosound";
        case 0x21 :	return "Setprt";
        case 0x22 :	return "Kbdvbase";
        case 0x23 :	return "Kbrate";
        case 0x24 :	return "Prtblk";
        case 0x25 :	return "Vsync";
        case 0x26 :	return "Supexec";
        case 0x27 :	return "Puntaes";
        case 0x29 :	return "Floprate 1.04";
        case 0x2A :	return "DMAread";
        case 0x2B :	return "DMAwrite";
        case 0x2C :	return "Bconmap 2";
        case 0x2E :	return "NVMaccess 3";
        case 0x2F :	return "Waketime 2.06, ST-Book";
        case 0x40 :	return "Blitmode";
        case 0x54 :	return "EsetPalette";
        case 0x55 :	return "EgetPalette";
        case 0x56 :	return "EsetGray";
        case 0x57 :	return "EsetSmear";
        case 0x58 :	return "Vsetmode (Falcon)";
        case 0x59 :	return "mon_type or VgetMonitor (Falcon)";
        case 0x5A :	return "VsetSync (Falcon)";
        case 0x5B :	return "VgetSize (Falcon)";
        case 0x5C :	return "VsetVars (Falcon)";
        case 0x5D :	return "VsetRGB (Falcon)";
        case 0x5E :	return "VgetRGB (Falcon)";
        case 0x5F :	return "VcheckMode (Falcon), MilanTOS";
        case 0x60 :	return "Dsp_DoBlock (Falcon)";
        case 0x61 :	return "Dsp_BlkHandShake (Falcon)";
        case 0x62 :	return "Dsp_BlkUnpacked (Falcon)";
        case 0x63 :	return "Dsp_InStream (Falcon)";
        case 0x64 :	return "Dsp_OutStream (Falcon)";
        case 0x65 :	return "Dsp_IOStream (Falcon)";
        case 0x66 :	return "Dsp_RemoveInterrupts (Falcon)";
        case 0x67 :	return "Dsp_GetWordSize (Falcon)";
        case 0x68 :	return "Dsp_Lock (Falcon)";
        case 0x69 :	return "Dsp_Unlock (Falcon)";
        case 0x6A :	return "Dsp_Available (Falcon)";
        case 0x6B :	return "Dsp_Reserve (Falcon)";
        case 0x6C :	return "Dsp_LoadProg (Falcon)";
        case 0x6D :	return "Dsp_ExecProg (Falcon)";
        case 0x6E :	return "Dsp_ExecBoot (Falcon)";
        case 0x6F :	return "Dsp_LodToBinary (Falcon)";
        case 0x70 :	return "Dsp_TriggerHC (Falcon)";
        case 0x71 :	return "Dsp_RequestUniqueAbility (Falcon)";
        case 0x72 :	return "Dsp_GetProgAbility (Falcon)";
        case 0x73 :	return "Dsp_FlushSubroutines (Falcon)";
        case 0x74 :	return "Dsp_LoadSubroutine (Falcon)";
        case 0x75 :	return "Dsp_InqSubrAbility (Falcon)";
        case 0x76 :	return "Dsp_RunSubroutine (Falcon)";
        case 0x77 :	return "Dsp_Hf0 (Falcon)";
        case 0x78 :	return "Dsp_Hf1 (Falcon)";
        case 0x79 :	return "Dsp_Hf2 (Falcon)";
        case 0x7A :	return "Dsp_Hf3 (Falcon)";
        case 0x7B :	return "Dsp_BlkWords (Falcon)";
        case 0x7C :	return "Dsp_BlkBytes (Falcon)";
        case 0x7D :	return "Dsp_HStat (Falcon)";
        case 0x7E :	return "Dsp_SetVectors (Falcon)";
        case 0x7F :	return "Dsp_MultBlocks (Falcon)";
        case 0x80 :	return "locksnd (Falcon)";
        case 0x81 :	return "unlocksnd (Falcon)";
        case 0x82 :	return "soundcmd (Falcon)";
        case 0x83 :	return "setbuffer (Falcon)";
        case 0x84 :	return "setmode (Falcon)";
        case 0x85 :	return "settracks (Falcon)";
        case 0x86 :	return "setmontracks (Falcon)";
        case 0x87 :	return "setinterrupt (Falcon)";
        case 0x88 :	return "buffoper (Falcon)";
        case 0x89 :	return "dsptristate (Falcon)";
        case 0x8A :	return "gpio (Falcon)";
        case 0x8B :	return "devconnect (Falcon)";
        case 0x8C :	return "sndstatus (Falcon)";
        case 0x8D :	return "buffptr (Falcon)";
        case 0x96 :	return "VsetMask";
        case 0xF9 :	return "Set Hatari CPU frequency 	Hatari DHS version only";
        case 0xFA :	return "Dump all registers to console 	Hatari DHS version only";
        case 0xFB :	return "Enter Hatari debug UI 	Hatari DHS version only";
        case 0xFC :	return "Stop a cycle counter 	Hatari DHS version only";
        case 0xFD :	return "Start or restart a cycle counter 	Hatari DHS version only";
        case 0xFE :	return "Debug output to console 	Hatari DHS version only";
        case 0xFF :	return "Change Emulator Options (DHS version) 	Hatari and DHS version";
    }
    return "Unknown";
}

const char* GetLineAName(uint16_t id)
{
    switch (id)
    {
    case 0xa000: return "linea_init";
    case 0xa001: return "put_pixel";
    case 0xa002: return "get_pixel";
    case 0xa003: return "draw_line";
    case 0xa004: return "horizontal_line";
    case 0xa005: return "filled_rect";
    case 0xa006: return "filled_polygon";
    case 0xa007: return "bit_blt";
    case 0xa008: return "text_blt";
    case 0xa009: return "show_mouse";
    case 0xa00a: return "hide_mouse";
    case 0xa00b: return "transform_mouse";
    case 0xa00c: return "undraw_sprite";
    case 0xa00d: return "draw_sprite";
    case 0xa00e: return "copy_raster";
    case 0xa00f: return "seed_fill";
    }
    return "Unknown";
}
QString GetTrapAnnotation(uint8_t trapNum, uint16_t callId)
{
    if (trapNum == 1)
    {
        const char* name = GetGemdosName(callId);
        return QString::asprintf("GEMDOS $%x %s", callId, name);
    }
    else if (trapNum == 13)
    {
        const char* name = GetBiosName(callId);
        return QString::asprintf("BIOS $%x %s", callId, name);
    }
    else if (trapNum == 14)
    {
        const char* name = GetXbiosName(callId);
        return QString::asprintf("XBIOS $%x %s", callId, name);
    }
    return "Unknown trap #";
}

QString GetTOSAnnotation(const Memory& mem, uint32_t address, const hop68::instruction& inst)
{
    uint32_t prevInst;
    if (inst.opcode == hop68::TRAP && mem.ReadCpuMulti(address - 4, 4, prevInst))
    {
        if ((prevInst >> 16) == 0x3f3c) // "move.w #xx,-(a7)
        {
            uint8_t trapNum = inst.op0.imm.val0;
            uint16_t callId = prevInst & 0xffff;
            return GetTrapAnnotation(trapNum, callId);
        }
    }
    else if (inst.opcode == hop68::NONE && (inst.header >> 12) == 0xa)
    {
        // Line A opcode
        return QString::asprintf("Line-A %s", GetLineAName(inst.header));
    }
    return QString();
}
