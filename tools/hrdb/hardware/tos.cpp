#include "tos.h"
#include "../hopper68/instruction68.h"
#include "../models/memory.h"
#include "stgen.h"
#include "gemdos.h"
#include "xbios.h"
#include "bios.h"

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

static QString MakeAnnotationString(const char* name, const char* desc)
{
    if (strlen(desc) != 0)
        return QString::asprintf("%s [%s]", name, desc);
    return QString(name);
}

QString GetTrapAnnotation(uint8_t trapNum, uint16_t callId)
{
    if (trapNum == 1)
    {
        Gemdos::Opcode op = (Gemdos::Opcode) callId;
        const char* name = Gemdos::GetEnumString(op);
        const char* desc = Gemdos::GetString(op);
        return QString::asprintf("GEMDOS $%x: ", callId) + MakeAnnotationString(name, desc);
    }
    else if (trapNum == 13)
    {
        Bios::Opcode op = (Bios::Opcode) callId;
        const char* name = Bios::GetEnumString(op);
        const char* desc = Bios::GetString(op);
        return QString::asprintf("BIOS $%x: ", callId) + MakeAnnotationString(name, desc);
    }
    else if (trapNum == 14)
    {
        Xbios::Opcode op = (Xbios::Opcode) callId;
        const char* name = Xbios::GetEnumString(op);
        const char* desc = Xbios::GetString(op);
        return QString::asprintf("XBIOS $%x: ", callId) + MakeAnnotationString(name, desc);
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
