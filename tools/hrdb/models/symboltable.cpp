#include "symboltable.h"
#include <assert.h>
#include <algorithm>

#include "../hardware/regs_st.h"
#define ADD_SYM(symname, addr, size, comment)\
    table.AddSymbol(#symname, addr, size, "H", comment);

class SymbolNameCompare
{
public:
    // This is the "less" comparison
    bool operator()(const Symbol &lhs, const Symbol &rhs) const
    {
        return lhs.name < rhs.name;
    }
};

static void AddHardware(SymbolSubTable& table)
{
    ADD_SYM(VID_MEMCONF		, 0xff8001, 1, "Memory Configuration")
    ADD_SYM(VID_DBASEHI 	, 0xff8201, 1, "Video Display Base High")
    ADD_SYM(VID_DBASEMID	, 0xff8203, 1, "Video Display Base Mid")
    ADD_SYM(VID_VCOUNTHI	, 0xff8205, 1, "Video Display Counter High")
    ADD_SYM(VID_VCOUNTMID	, 0xff8207, 1, "Video Display Counter Mid")
    ADD_SYM(VID_VCOUNTLOW	, 0xff8209, 1, "Video Display Counter Low")
    ADD_SYM(VID_SYNCMODE	, 0xff820a, 1, "Video Sync Mode")
    ADD_SYM(VID_DBASELO_STE	, 0xff820d, 1, "Video Display Base Low (STE)")
    ADD_SYM(VID_WIDTHOFF_STE, 0xff820f, 1, "Video Width Offset (STE)")
    ADD_SYM(VID_COLOR0		, 0xff8240, 2, "Video Palette Colour 0")
    ADD_SYM(VID_COLOR1		, 0xff8242, 2, "Video Palette Colour 1")
    ADD_SYM(VID_COLOR2		, 0xff8244, 2, "Video Palette Colour 2")
    ADD_SYM(VID_COLOR3		, 0xff8246, 2, "Video Palette Colour 3")
    ADD_SYM(VID_COLOR4		, 0xff8248, 2, "Video Palette Colour 4")
    ADD_SYM(VID_COLOR5		, 0xff824a, 2, "Video Palette Colour 5")
    ADD_SYM(VID_COLOR6		, 0xff824c, 2, "Video Palette Colour 6")
    ADD_SYM(VID_COLOR7		, 0xff824e, 2, "Video Palette Colour 7")
    ADD_SYM(VID_COLOR8		, 0xff8250, 2, "Video Palette Colour 8")
    ADD_SYM(VID_COLOR9		, 0xff8252, 2, "Video Palette Colour 9")
    ADD_SYM(VID_COLOR10		, 0xff8254, 2, "Video Palette Colour 10")
    ADD_SYM(VID_COLOR11		, 0xff8256, 2, "Video Palette Colour 11")
    ADD_SYM(VID_COLOR12		, 0xff8258, 2, "Video Palette Colour 12")
    ADD_SYM(VID_COLOR13		, 0xff825a, 2, "Video Palette Colour 13")
    ADD_SYM(VID_COLOR14		, 0xff825c, 2, "Video Palette Colour 14")
    ADD_SYM(VID_COLOR15		, 0xff825e, 2, "Video Palette Colour 15")
    ADD_SYM(VID_HSCROLL_A	, 0xff8264, 1, "Video Hardware Scroll (STE)")
    ADD_SYM(VID_HSCROLL_B	, 0xff8265, 1, "Video Hardware Scroll (STE)")
    ADD_SYM(VID_SHIFTMD		, 0xff8260, 1, "Video Shifter Mode (ST)")
    ADD_SYM(VID_SHIFTMD_TT  , 0xff8262, 1, "Video Shifter Mode (TT)")
    ADD_SYM(DMA_DISKCTL		, 0xff8604, 1, "Disk Controller Data Access")	// disk controller data access
    ADD_SYM(DMA_MODE		, 0xff8606, 1, "DMA Mode/Status Register")
    ADD_SYM(DMA_DMAHIGH		, 0xff8609, 1, "DMA Base High")
    ADD_SYM(DMA_DMAMID		, 0xff860b, 1, "DMA Base Mid")
    ADD_SYM(DMA_DMALOW		, 0xff860d, 1, "DMA Base Low")
    ADD_SYM(YM_GISELECT     , 0xff8800, 1, "YM2149 Register Select/Data Read")
    ADD_SYM(YM_GIWRITE		, 0xff8802, 1, "YM2149 Register Data Write")
    ADD_SYM(MFP_GPIP        , Regs::MFP_GPIP , 1, "MFP General Purpose I/O")
    ADD_SYM(MFP_AER         , Regs::MFP_AER  , 1, "MFP Active Edge Register ")
    ADD_SYM(MFP_DDR         , Regs::MFP_DDR  , 1, "MFP Data Direction Register")
    ADD_SYM(MFP_IERA        , Regs::MFP_IERA , 1, "MFP Interrupt Enable A ")
    ADD_SYM(MFP_IERB        , Regs::MFP_IERB , 1, "MFP Interrupt Enable B")
    ADD_SYM(MFP_IPRA        , Regs::MFP_IPRA , 1, "MFP Interrupt Pending A")
    ADD_SYM(MFP_IPRB        , Regs::MFP_IPRB , 1, "MFP Interrupt Pending B")
    ADD_SYM(MFP_ISRA        , Regs::MFP_ISRA , 1, "MFP Interrupt In Service A")
    ADD_SYM(MFP_ISRB        , Regs::MFP_ISRB , 1, "MFP Interrupt In Service B")
    ADD_SYM(MFP_IMRA        , Regs::MFP_IMRA , 1, "MFP Interrupt Mask A")
    ADD_SYM(MFP_IMRB        , Regs::MFP_IMRB , 1, "MFP Interrupt Mask B")
    ADD_SYM(MFP_VR          , Regs::MFP_VR   , 1, "MFP Vector Base Register")
    ADD_SYM(MFP_TACR        , Regs::MFP_TACR , 1, "MFP Timer A Control")
    ADD_SYM(MFP_TBCR        , Regs::MFP_TBCR , 1, "MFP Timer B Control")
    ADD_SYM(MFP_TCDCR       , Regs::MFP_TCDCR, 1, "MFP Timer C&D Control")
    ADD_SYM(MFP_TADR        , Regs::MFP_TADR , 1, "MFP Timer A Data")
    ADD_SYM(MFP_TBDR        , Regs::MFP_TBDR , 1, "MFP Timer B Data")
    ADD_SYM(MFP_TCDR        , Regs::MFP_TCDR , 1, "MFP Timer C Data")
    ADD_SYM(MFP_TDDR        , Regs::MFP_TDDR , 1, "MFP Timer D Data")
    ADD_SYM(MFP_SCR         , Regs::MFP_SCR  , 1, "MFP Sync Char Register")
    ADD_SYM(MFP_UCR         , Regs::MFP_UCR  , 1, "MFP USART Control ")
    ADD_SYM(MFP_RSR         , Regs::MFP_RSR  , 1, "MFP Receiver Status")
    ADD_SYM(MFP_TSR         , Regs::MFP_TSR  , 1, "MFP Transmit Status")
    ADD_SYM(MFP_UDR         , Regs::MFP_UDR  , 1, "MFP Usart Data")
    ADD_SYM(ACIA_KEYCTL     , 0xfffc00       , 1, "ACIA Keyboard Control")
    ADD_SYM(ACIA_KEYBD      , 0xfffc02       , 1, "ACIA Keyboard Data")
    ADD_SYM(ACIA_MIDICTL	, 0xfffc04       , 1, "ACIA MIDI Control")
    ADD_SYM(ACIA_MIDID      , 0xfffc06       , 1, "ACIA MIDI Data")

    // DMA sounds
    ADD_SYM(DMASND_BUFINTS_STE, 0xff8900, 1, "DMA Sound Buffer Interrupts (STE)")
    ADD_SYM(DMASND_CTRL_STE,    0xff8901, 1, "DMA Sound Control (STE)")
    ADD_SYM(DMASND_STARTH_STE,  0xff8903, 1, "DMA Sound Buffer Start High (STE)")
    ADD_SYM(DMASND_STARTM_STE,  0xff8905, 1, "DMA Sound Buffer Start Mid (STE)")
    ADD_SYM(DMASND_STARTL_STE,  0xff8907, 1, "DMA Sound Buffer Start Low (STE)")
    ADD_SYM(DMASND_CURRH_STE,   0xff8909, 1, "DMA Sound Buffer Current High (STE)")
    ADD_SYM(DMASND_CURRM_STE,   0xff890b, 1, "DMA Sound Buffer Current Mid (STE)")
    ADD_SYM(DMASND_CURRL_STE,   0xff890d, 1, "DMA Sound Buffer Current Low (STE)")
    ADD_SYM(DMASND_ENDH_STE,    0xff890f, 1, "DMA Sound Buffer End High (STE)")
    ADD_SYM(DMASND_ENDM_STE,    0xff8911, 1, "DMA Sound Buffer End Mid (STE)")
    ADD_SYM(DMASND_ENDL_STE,    0xff8913, 1, "DMA Sound Buffer End Low (STE)")

    // STE
    ADD_SYM(BLT_HALFTONE_0  , 0xff8a00, 2, "Blitter Halftone RAM (STE)")
    ADD_SYM(BLT_SRC_INC_X	, 0xff8a20, 2, "Blitter Source Increment X (STE)")
    ADD_SYM(BLT_SRC_INC_Y	, 0xff8a22, 2, "Blitter Source Increment Y (STE)")
    ADD_SYM(BLT_SRC_ADDR_L	, 0xff8a24, 2, "Blitter Source Address (STE)")
    ADD_SYM(BLT_ENDMASK_1	, 0xff8a28, 2, "Blitter Endmask 1 Left (STE)")
    ADD_SYM(BLT_ENDMASK_2	, 0xff8a2a, 2, "Blitter Endmask 2 Middle (STE)")
    ADD_SYM(BLT_ENDMASK_3	, 0xff8a2c, 2, "Blitter Endmask 3 Right (STE)")
    ADD_SYM(BLT_DST_INC_X	, 0xff8a2e, 2, "Blitter Destination Increment X (STE)")
    ADD_SYM(BLT_DST_INC_Y	, 0xff8a30, 2, "Blitter Destination Increment Y (STE)")
    ADD_SYM(BLT_DST_ADDR_L	, 0xff8a32, 2, "Blitter Destination Address (STE)")
    ADD_SYM(BLT_COUNT_X     , 0xff8a36, 2, "Blitter Count X (STE)")
    ADD_SYM(BLT_COUNT_Y     , 0xff8a38, 2, "Blitter Count Y (STE)")
    ADD_SYM(BLT_HOP         , 0xff8a3a, 1, "Blitter Halftone Operation (STE)")
    ADD_SYM(BLT_OP          , 0xff8a3b, 1, "Blitter Combine Operation (STE)")
    ADD_SYM(BLT_MISC_1      , 0xff8a3c, 1, "Blitter Misc 1 (STE)")
    ADD_SYM(BLT_MISC_2      , 0xff8a3d, 1, "Blitter Misc 2 (STE)")

    ADD_SYM(JOY_BUTTONS     , 0xff9200, 2, "Joytstick Buttons (STE)")
    ADD_SYM(JOY_INPUTS      , 0xff9202, 2, "Joytstick Input / Read Mask (STE)")
    ADD_SYM(PADDLE_X_0_POS  , 0xff9210, 2, "X Paddle 0 Position (STE)")
    ADD_SYM(PADDLE_Y_0_POS  , 0xff9212, 2, "Y Paddle 0 Position (STE)")
    ADD_SYM(PADDLE_X_1_POS  , 0xff9214, 2, "X Paddle 1 Position (STE)")
    ADD_SYM(PADDLE_Y_1_POS  , 0xff9216, 2, "Y Paddle 1 Position (STE)")
    ADD_SYM(LIGHTPEN_X_POS  , 0xff9220, 2, "Lightpen X-Position (STE)")
    ADD_SYM(LIGHTPEN_Y_POS  , 0xff9222, 2, "Lightpen Y-Position (STE)")

    // VIDEL palette
    ADD_SYM(VIDEL_PALETTE   , 0xFF9800, 4*256, "VIDEL Palette Registers (Falcon)")

    // DSP
    ADD_SYM(DSP_INT_CTRL    , 0xFFA200, 1, "DSP Interrupt Ctrl Register (Falcon)")
    ADD_SYM(DSP_CMD_VEC     , 0xFFA201, 1, "DSP Command Vector Register (Falcon)")
    ADD_SYM(DSP_INT_STATUS  , 0xFFA202, 1, "DSP Interrupt Status Register (Falcon)")
    ADD_SYM(DSP_INT_VEC     , 0xFFA203, 1, "DSP Interrupt Vector Register (Falcon)")

    ADD_SYM(DSP_DATA_ALL    , 0xFFA204, 4, "DSP Data Longword (Falcon)")
    ADD_SYM(DSP_DATA_HI     , 0xFFA205, 1, "DSP Data High (Falcon)")
    ADD_SYM(DSP_DATA_MID    , 0xFFA206, 1, "DSP Data Mid (Falcon)")
    ADD_SYM(DSP_DATA_LO     , 0xFFA207, 1, "DSP Data Low (Falcon)")

    // TOS variables
    ADD_SYM(etv_timer       , 0x400, 4, "vector for timer interrupt chain")
    ADD_SYM(etv_critic      , 0x404, 4, "vector for critical error chain")
    ADD_SYM(etv_term        , 0x408, 4, "vector for process terminate")
    ADD_SYM(etv_xtra        , 0x40c, 20, "5 reserved vectors")
    ADD_SYM(memvalid        , 0x420, 4, "indicates system state on RESET")
    ADD_SYM(memcntlr        , 0x424, 2, "mem controller config nibble")
    ADD_SYM(resvalid        , 0x426, 4, "validates 'resvector'")
    ADD_SYM(resvector       , 0x42a, 4, "[RESET] bailout vector")
    ADD_SYM(phystop         , 0x42e, 4, "physical top of RAM")
    ADD_SYM(_membot         , 0x432, 4, "bottom of available memory")
    ADD_SYM(_memtop         , 0x436, 4, "top of available memory")
    ADD_SYM(memval2         , 0x43a, 4, "validates 'memcntlr' and 'memconf'")
    ADD_SYM(flock           , 0x43e, 2, "floppy disk/FIFO lock variable")
    ADD_SYM(seekrate        , 0x440, 2, "default floppy seek rate")
    ADD_SYM(_timr_ms        , 0x442, 2, "system timer calibration (in ms)")
    ADD_SYM(_fverify        , 0x444, 2, "nonzero: verify on floppy write")
    ADD_SYM(_bootdev        , 0x446, 2, "default boot device")
    ADD_SYM(palmode         , 0x448, 2, "nonzero ==> PAL mode")
    ADD_SYM(defshiftmd      , 0x44a, 2, "default video rez (first byte)")
    ADD_SYM(sshiftmd        , 0x44c, 2, "shadow for 'shiftmd' register")
    ADD_SYM(_v_bas_ad       , 0x44e, 4, "pointer to base of screen memory")
    ADD_SYM(vblsem          , 0x452, 2, "semaphore to enforce mutex invbl")
    ADD_SYM(nvbls           , 0x454, 4, "number of deferred vectors")
    ADD_SYM(_vblqueue       , 0x456, 4, "pointer to vector of deferredvfuncs")
    ADD_SYM(colorptr        , 0x45a, 4, "pointer to palette setup (or NULL)")
    ADD_SYM(screenpt        , 0x45e, 4, "pointer to screen base setup (or NULL)")
    ADD_SYM(_vbclock        , 0x462, 4, "count of unblocked vblanks")
    ADD_SYM(_frclock        , 0x466, 4, "count of every vblank")
    ADD_SYM(hdv_init        , 0x46a, 4, "hard disk initialization")
    ADD_SYM(swv_vec         , 0x46e, 4, "video change-resolution bailout")
    ADD_SYM(hdv_bpb         , 0x472, 4, "disk 'get BPB'")
    ADD_SYM(hdv_rw          , 0x476, 4, "disk read/write")
    ADD_SYM(hdv_boot        , 0x47a, 4, "disk 'get boot sector'")
    ADD_SYM(hdv_mediach     , 0x47e, 4, "disk media change detect")
    ADD_SYM(_cmdload        , 0x482, 2, "nonzero: load COMMAND.COM from boot")
    ADD_SYM(conterm         , 0x484, 2, "console/vt52 bitSwitches (%%0..%%2)")
    ADD_SYM(trp14ret        , 0x486, 4, "saved return addr for _trap14")
    ADD_SYM(criticret       , 0x48a, 4, "saved return addr for _critic")
    ADD_SYM(themd           , 0x48e, 4, "memory descriptor (MD)")
    ADD_SYM(_____md         , 0x49e, 4, "(more Memory Descriptor)")
    ADD_SYM(savptr          , 0x4a2, 4, "pointer to register save area")
    ADD_SYM(_nflops         , 0x4a6, 2, "number of disks attached (0, 1+)")
    ADD_SYM(con_state       , 0x4a8, 4, "state of conout() parser")
    ADD_SYM(save_row        , 0x4ac, 2, "saved row# for cursor X-Y addressing")
    ADD_SYM(sav_context     , 0x4ae, 4, "pointer to saved processor context")
    ADD_SYM(_bufl           , 0x4b2, 8, "two buffer-list headers")
    ADD_SYM(_hz_200         , 0x4ba, 4, "200hz raw system timer tick")
    ADD_SYM(_drvbits        , 0x4c2, 4, "bit vector of 'live' block devices")
    ADD_SYM(_dskbufp        , 0x4c6, 4, "pointer to common disk buffer")
    ADD_SYM(_autopath       , 0x4ca, 4, "pointer to autoexec path (or NULL)")
    ADD_SYM(_vbl_list       , 0x4ce, 4, "initial _vblqueue (to $4ee)")
    ADD_SYM(_dumpflg        , 0x4ee, 2, "screen-dump flag")
    ADD_SYM(_prtabt         , 0x4f0, 4, "printer abort flag")
    ADD_SYM(_sysbase        , 0x4f2, 4, "-> base of OS")
    ADD_SYM(_shell_p        , 0x4f6, 4, "-> global shell info")
    ADD_SYM(end_os          , 0x4fa, 4, "-> end of OS memory usage")
    ADD_SYM(exec_os         , 0x4fe, 4, "-> address of shell to exec on startup")
    ADD_SYM(scr_dump        , 0x502, 4, "-> screen dump code")
    ADD_SYM(prv_lsto        , 0x506, 4, "-> _lstostat()")
    ADD_SYM(prv_lst         , 0x50a, 4, "-> _lstout()")
    ADD_SYM(prv_auxo        , 0x50e, 4, "-> _auxostat()")
    ADD_SYM(prv_aux         , 0x512, 4, "-> _auxout()")
    ADD_SYM(user_mem        ,0x1000, 1, "User Memory")

    ADD_SYM(tos_512         ,0xe00000, 512 * 1024, "TOS ROM (512K)")
    ADD_SYM(tos_192         ,0xfc0000, 256 * 1024, "TOS ROM (192K)")

    ADD_SYM(cart            ,0xfa0000, 0x30000, "Cartridge ROM")
    // Low vectors
    ADD_SYM(__vec_buserr,  0x8,  4, "Bus Error Vector")
    ADD_SYM(__vec_addrerr, 0xc,  4, "Address Error Vector")
    ADD_SYM(__vec_illegal, 0x10, 4, "Illegal Instruction Vector")
    ADD_SYM(__vec_zerodiv, 0x14, 4, "Zero Divide Vector")
    ADD_SYM(__vec_chk,     0x18, 4, "CHK Instruction Vector")
    ADD_SYM(__vec_trapcc,  0x1c, 4, "TRAPcc Instruction Vector")
    ADD_SYM(__vec_privinst,0x20, 4, "Privileged Instruction Vector")
    ADD_SYM(__vec_trace,   0x24, 4, "Trace Vector")
    ADD_SYM(__vec_linea,   0x28, 4, "Line-A Vector")
    ADD_SYM(__vec_linef,   0x2c, 4, "Line-F Vector")

    ADD_SYM(__vec_hbl,     0x68, 4, "HBL Auto-Vector")
    ADD_SYM(__vec_vbl,     0x70, 4, "VBL Auto-Vector")
    ADD_SYM(__vec_mfp,     0x78, 4, "MFP Auto-Vector")

    ADD_SYM(__vec_trap0,     0x80, 4, "Trap #0 Vector (GEMDOS)")
    ADD_SYM(__vec_trap1,     0x84, 4, "Trap #1 Vector")
    ADD_SYM(__vec_trap2,     0x88, 4, "Trap #2 Vector")
    ADD_SYM(__vec_trap3,     0x8c, 4, "Trap #3 Vector")
    ADD_SYM(__vec_trap4,     0x90, 4, "Trap #4 Vector")
    ADD_SYM(__vec_trap5,     0x94, 4, "Trap #5 Vector")
    ADD_SYM(__vec_trap6,     0x98, 4, "Trap #6 Vector")
    ADD_SYM(__vec_trap7,     0x9c, 4, "Trap #7 Vector")
    ADD_SYM(__vec_trap8,     0xa0, 4, "Trap #8 Vector")
    ADD_SYM(__vec_trap9,     0xa4, 4, "Trap #9 Vector")
    ADD_SYM(__vec_trap10,    0xa8, 4, "Trap #10 Vector")
    ADD_SYM(__vec_trap11,    0xac, 4, "Trap #11 Vector")
    ADD_SYM(__vec_trap12,    0xb0, 4, "Trap #12 Vector")
    ADD_SYM(__vec_trap13,    0xb4, 4, "Trap #13 Vector (BIOS)")
    ADD_SYM(__vec_trap14,    0xb8, 4, "Trap #14 Vector (XBIOS)")
    // There is no trap 15 :)

    ADD_SYM(__vec_mfp_cent,    0x100, 4, "MFP Centronics Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_dcd,     0x104, 4, "MFP DCD Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_cts,     0x108, 4, "MFP CTS Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_blit,    0x10c, 4, "MFP Blitter Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_timerd,  0x110, 4, "MFP Timer D Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_timerc,  0x114, 4, "MFP Timer C Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_acia,    0x118, 4, "MFP ACIA Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_fdc,     0x11c, 4, "MFP Floppy Disk Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_timerb,  0x120, 4, "MFP Timer B Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_senderr, 0x124, 4, "MFP Send Error Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_sendemp, 0x128, 4, "MFP Send Emp Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_recerr,  0x12c, 4, "MFP Receive Error Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_recfull, 0x130, 4, "MFP Receive Full Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_timera,  0x134, 4, "MFP Timer A Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_ringd,   0x138, 4, "MFP RINGD Interrupt Vector (default)")
    ADD_SYM(__vec_mfp_mono,    0x13c, 4, "MFP Mono Interrupt Vector (default)")
}

static void AddHardwareP(SymbolSubTable& table)
{
    ADD_SYM(__INT_RESET           ,   0,  1,  "Interrupt Vector: reset")
    ADD_SYM(__INT_STACKERR        ,   1,  1,  "Interrupt Vector: stack-error")
    ADD_SYM(__INT_TRACE           ,   2,  1,  "Interrupt Vector: trace")
    ADD_SYM(__INT_SWI             ,   3,  1,  "Interrupt Vector: SWI")
    ADD_SYM(__INT_IRQA            ,   4,  1,  "Interrupt Vector: IRQA")
    ADD_SYM(__INT_IRQB            ,   5,  1,  "Interrupt Vector: IRQB")
    ADD_SYM(__INT_SSI_RCV_DATA    ,   6,  1,  "Interrupt Vector: SSI RX")
    ADD_SYM(__INT_SSI_RCV_DATA_E  ,   7,  1,  "Interrupt Vector: SSI RX ext")
    ADD_SYM(__INT_SSI_TRX_DATA    ,   8,  1,  "Interrupt Vector: SSI TX")
    ADD_SYM(__INT_SSI_TRX_DATA_E  ,   9,  1,  "Interrupt Vector: SSI TX ext")
    ADD_SYM(__INT_SCI_RCV_DATA    ,   10, 1,  "Interrupt Vector: SCI RX")
    ADD_SYM(__INT_SCI_RCV_DATA_E  ,   11, 1,  "Interrupt Vector: SCI RX ext")
    ADD_SYM(__INT_SCI_TRX_DATA    ,   12, 1,  "Interrupt Vector: SCI TX")
    ADD_SYM(__INT_NMI             ,   15, 1,  "Interrupt Vector: NMI")
    ADD_SYM(__INT_HOST_RCV_DATA   ,   16, 1,  "Interrupt Vector: Host RX")
    ADD_SYM(__INT_HOST_TRX_DATA   ,   17, 1,  "Interrupt Vector: Host TX")
    ADD_SYM(__INT_HOST_COMMAND    ,   18, 1,  "Interrupt Vector: Host Command")
}

static void AddHardwareX(SymbolSubTable& table)
{
    ADD_SYM(__PBC		, 0xffc0 + 0x20, 1, "Port B control register")
    ADD_SYM(__PCC		, 0xffc0 + 0x21, 1, "Port C control register")
    ADD_SYM(__PBDDR		, 0xffc0 + 0x22, 1, "Port B data direction register")
    ADD_SYM(__PCDDR		, 0xffc0 + 0x23, 1, "Port C data direction register")
    ADD_SYM(__PBD		, 0xffc0 + 0x24, 1, "Port B data register")
    ADD_SYM(__PCD		, 0xffc0 + 0x25, 1, "Port C data register")
    ADD_SYM(__HOST_HCR	, 0xffc0 + 0x28, 1, "Host control register")
    ADD_SYM(__HOST_HSR	, 0xffc0 + 0x29, 1, "Host status register")
    ADD_SYM(__HOST_HRX	, 0xffc0 + 0x2b, 1, "Host RX register")
    ADD_SYM(__HOST_HTX	, 0xffc0 + 0x2b, 1, "Host TX register")
    ADD_SYM(__SSI_CRA	, 0xffc0 + 0x2c, 1, "SSI control register A")
    ADD_SYM(__SSI_CRB	, 0xffc0 + 0x2d, 1, "SSI control register B")
    ADD_SYM(__SSI_SR	, 0xffc0 + 0x2e, 1, "SSI status register")
    ADD_SYM(__SSI_TSR	, 0xffc0 + 0x2e, 1, "SSI time slot register")
    ADD_SYM(__SSI_RX	, 0xffc0 + 0x2f, 1, "SSI RX register")
    ADD_SYM(__SSI_TX	, 0xffc0 + 0x2f, 1, "SSI TX register")
    ADD_SYM(__SCI_SCR	, 0xffc0 + 0x30, 1, "SCI control register")
    ADD_SYM(__SCI_SSR	, 0xffc0 + 0x31, 1, "SCI status register")
    ADD_SYM(__SCI_SCCR	, 0xffc0 + 0x32, 1, "SCI clock control register")
    ADD_SYM(__BCR		, 0xffc0 + 0x3e, 1, "Port A bus control register")
    ADD_SYM(__IPR		, 0xffc0 + 0x3f, 1, "Interrupt priority register")
}

void SymbolSubTable::Clear()
{
    m_symbols.clear();
    m_addrKeys.clear();
    m_addrLookup.clear();
}

void SymbolSubTable::AddSymbol(std::string name, uint32_t address, uint32_t size, std::string type,
                               const std::string& comment)
{
    Symbol sym;
    sym.name = name;
    sym.address = address;
    sym.type = type;     // hardware
    sym.size = size;
    sym.comment = comment;
    m_symbols.push_back(sym);
}

void SymbolSubTable::CreateCache()
{
    // Sort the symbols in name order
    std::sort(m_symbols.begin(), m_symbols.end(), SymbolNameCompare());

    // Reset the lookup keys, since the order has changed
    m_addrLookup.clear();
    for (size_t i = 0; i < m_symbols.size(); ++i)
    {
        Symbol& s = m_symbols[i];
        m_addrLookup.insert(Pair(s.address, i));
    }

    // Recalc the keys table
    m_addrKeys.clear();
    Map::iterator it(m_addrLookup.begin());
    size_t index = 0;
    while (it != m_addrLookup.end())
    {
        m_addrKeys.push_back(*it);

        size_t symIndex = it->second;
        m_symbols[symIndex].index = index;
        ++index;
        ++it;
    }
}

bool SymbolSubTable::Find(uint32_t address, Symbol &result) const
{
    Map::const_iterator it = m_addrLookup.find(address);
    if (it == m_addrLookup.end())
        return false;

    result = m_symbols[it->second];
    return true;
}

bool SymbolSubTable::FindLowerOrEqual(uint32_t address, bool sizeCheck, Symbol &result) const
{
    // Degenerate cases
    if (m_addrKeys.size() == 0)
        return false;

    if (address < m_symbols[m_addrKeys[0].second].address)
        return false;

    // Binary chop
    size_t lower = 0;
    size_t upper_plus_one = m_addrKeys.size();

    // We need to find the first item which is *lower or equal* to N,
    while ((lower + 1) < upper_plus_one)
    {
        size_t mid = (lower + upper_plus_one) / 2;

        const Symbol& midSym = m_symbols[m_addrKeys[mid].second];
        if (midSym.address <= address)
        {
            // mid is lower/equal, search
            lower = mid;
        }
        else {
            // mid is
            upper_plus_one = mid;
        }
    }

    result = m_symbols[m_addrKeys[lower].second];
    assert(address >= result.address);
    // Size checks
    if (result.size == 0)
        return true;        // unlimited size

    if (sizeCheck)
    {
        if (result.size > (address - result.address))
            return true;
        return false;
    }
    return true;
}

bool SymbolSubTable::Find(std::string name, Symbol &result) const
{
    for (size_t i = 0; i < this->m_symbols.size(); ++i)
    {
        if (m_symbols[i].name == name)
        {
            result = m_symbols[i];
            return true;
        }
    }
    return false;
}

const Symbol SymbolSubTable::Get(size_t index) const
{
    return m_symbols[index];
}

///////////////////////////////////////////////////////////////////////////////////////////////////
SymbolTable::SymbolTable()
{
}

void SymbolTable::InitHardware(MemSpace space)
{
    if (space == MEM_CPU)
        AddHardware(m_subTables[kHardware]);
    if (space == MEM_P)
        AddHardwareP(m_subTables[kHardware]);
    if (space == MEM_X)
        AddHardwareX(m_subTables[kHardware]);

    m_subTables[kHardware].CreateCache();
}

void SymbolTable::ResetHatari()
{
    m_subTables[kHatari].Clear();
}

void SymbolTable::SetHatariSubTable(const SymbolSubTable &subtable)
{
    m_subTables[kHatari] = subtable;
    m_subTables[kHatari].CreateCache();
}

size_t SymbolTable::Count() const
{
    size_t total = 0;
    for (int i = 0; i < kNumTables; ++i)
    {
        total += m_subTables[i].Count();
    }
    return total;
}

bool SymbolTable::Find(uint32_t address, Symbol &result) const
{
    for (int i = 0; i < kNumTables; ++i)
    {
        if (m_subTables[i].Find(address, result))
            return true;
    }
    return false;
}

bool SymbolTable::FindLowerOrEqual(uint32_t address, bool sizeCheck, Symbol &result) const
{
    // This is non-intuitive, but we need to find the *higher* symbol
    // in all the subtables (the one that's closest to the given address)
    uint32_t highest = 0;

    for (int i = 0; i < kNumTables; ++i)
    {
        Symbol tempRes;
        if (m_subTables[i].FindLowerOrEqual(address, sizeCheck, tempRes))
        {
            if (tempRes.address > highest)
            {
                result = tempRes;
                highest = tempRes.address;
            }
        }
    }
    return highest != 0;
}

bool SymbolTable::Find(std::string name, Symbol &result) const
{
    for (int i = 0; i < kNumTables; ++i)
    {
        if (m_subTables[i].Find(name, result))
            return true;
    }
    return false;
}

const Symbol SymbolTable::Get(size_t index) const
{
    for (int i = 0; i < kNumTables; ++i)
    {
        size_t thisCount = m_subTables[i].Count();
        if (index < thisCount)
            return m_subTables[i].Get(index);

        index -= thisCount;
    }
    assert(false);
    static Symbol dummy;
    return dummy;
}
