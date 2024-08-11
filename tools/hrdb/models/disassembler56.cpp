#include "disassembler56.h"
#include "hopper56/buffer.h"

using namespace hop56;
void Disassembler56::decode_inst(buffer_reader& buf, instruction& inst, const decode_settings& settings)
{
    decode(inst, buf, settings);
}

int Disassembler56::decode_buf(buffer_reader& buf, disassembly& disasm, const decode_settings& settings, uint32_t address, int32_t maxLines)
{
    while (buf.get_remain() >= 1)
    {
        line line;
        line.address = buf.get_pos() + address;

        // decode uses a copy of the buffer state
        {
            buffer_reader buf_copy(buf);
            decode(line.inst, buf_copy, settings);
        }

#if 0
        // Save copy of instruction memory
        {
            uint16_t count = line.inst.word_count;
            buffer_reader buf_copy(buf);
            buf_copy.read(line.mem, count);
        }
#endif
        // Handle failure
        disasm.lines.push_back(line);

        buf.advance(line.inst.word_count);
        if (disasm.lines.size() >= maxLines)
            break;
    }
    return 0;
}
