/*
  Hatari - stMemory.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  ST Memory access functions.
*/
const char STMemory_fileid[] = "Hatari stMemory.c : " __DATE__ " " __TIME__;

#include "stMemory.h"
#include "configuration.h"
#include "floppy.h"
#include "gemdos.h"
#include "ioMem.h"
#include "log.h"
#include "memory.h"
#include "memorySnapShot.h"
#include "tos.h"
#include "vdi.h"
#include "m68000.h"
#include "screen.h"
#include "video.h"

/* STRam points to our ST Ram. Unless the user enabled SMALL_MEM where we have
 * to save memory, this includes all TOS ROM and IO hardware areas for ease
 * and emulation speed - so we create a 16 MiB array directly here.
 * But when the user turned on ENABLE_SMALL_MEM, this only points to a malloc'ed
 * buffer with the ST RAM; the ROM and IO memory will be handled separately. */
#if ENABLE_SMALL_MEM
Uint8 *STRam;
#else
Uint8 STRam[16*1024*1024];
#endif

Uint32 STRamEnd;		/* End of ST Ram, above this address is no-mans-land and ROM/IO memory */



Uint32	RAM_Bank0_Size;		/* Physical RAM on board in bank0 (in bytes) : 128, 512 or 2048 KB */
Uint32	RAM_Bank1_Size;		/* Physical RAM on board in bank1 (in bytes) : 128, 512 or 2048 KB */

Uint32	MMU_Bank0_Size;		/* Logical MMU RAM size for bank0 (in bytes) : 128, 512 or 2048 KB */
Uint32	MMU_Bank1_Size;		/* Logical MMU RAM size for bank1 (in bytes) : 128, 512 or 2048 KB */

Uint8	MMU_Conf_Expected;	/* Expected value for $FF8001 corresponding to ST RAM size if <= 4MB */


static void	STMemory_MMU_ConfToBank ( Uint8 MMU_conf , Uint32 *pBank0 , Uint32 *pBank1 );
static int	STMemory_MMU_Size ( Uint8 MMU_conf );
static int	STMemory_MMU_Size_TT ( Uint8 MMU_conf );

static Uint32	STMemory_MMU_Translate_Addr_STF ( Uint32 addr_logical , int RAM_Bank_Size , int MMU_Bank_Size );
static Uint32	STMemory_MMU_Translate_Addr_STE ( Uint32 addr_logical , int RAM_Bank_Size , int MMU_Bank_Size );



/**
 * Set default value for MMU bank size and RAM bank size
 */
void	STMemory_Init ( int RAM_Size_Byte )
{
	STMemory_Reset ( true );

	if ( RAM_Size_Byte <= 0x400000 )
	{
		if ( STMemory_RAM_SetBankSize ( RAM_Size_Byte , &RAM_Bank0_Size , &RAM_Bank1_Size , &MMU_Conf_Expected ) == false )
		{
			fprintf ( stderr , "Error invalid RAM size %d KB for MMU banks\n", RAM_Size_Byte );
		}
	}
}


/*
 * Reset the internal MMU/MCU used to configure address decoding for the RAM banks
 * 0xFF8001 is set to 0 on cold reset but keep its value on warm reset
 */
void	STMemory_Reset ( bool bCold )
{
	if ( bCold )
	{
		IoMem[ 0xff8001 ] = 0x0;
		STMemory_MMU_ConfToBank ( IoMem[ 0xff8001 ] , &MMU_Bank0_Size , &MMU_Bank1_Size );
	}
}


/**
 * Clear section of ST's memory space.
 */
static void STMemory_Clear(Uint32 StartAddress, Uint32 EndAddress)
{
	memset(&STRam[StartAddress], 0, EndAddress-StartAddress);
}

/**
 * Copy given memory area safely to Atari RAM.
 * If the memory area isn't fully within RAM, only the valid parts are written.
 * Useful for all kinds of IO operations.
 * 
 * addr - destination Atari RAM address
 * src - source Hatari memory address
 * len - number of bytes to copy
 * name - name / description if this memory copy for error messages
 * 
 * Return true if whole copy was safe / valid.
 */
bool STMemory_SafeCopy(Uint32 addr, Uint8 *src, unsigned int len, const char *name)
{
	Uint32 end;

	if ( STMemory_CheckAreaType ( addr, len, ABFLAG_RAM ) )
	{
		memcpy(&STRam[addr], src, len);
		return true;
	}
	Log_Printf(LOG_WARN, "Invalid '%s' RAM range 0x%x+%i!\n", name, addr, len);

	for (end = addr + len; addr < end; addr++, src++)
	{
		if ( STMemory_CheckAreaType ( addr, 1, ABFLAG_RAM ) )
			STRam[addr] = *src;
	}
	return false;
}


/**
 * Save/Restore snapshot of RAM / ROM variables
 * ('MemorySnapShot_Store' handles type)
 */
void STMemory_MemorySnapShot_Capture(bool bSave)
{
	MemorySnapShot_Store(&STRamEnd, sizeof(STRamEnd));

	/* After restoring RAM/MMU bank sizes we must call memory_map_Standard_RAM() */
	MemorySnapShot_Store(&RAM_Bank0_Size, sizeof(RAM_Bank0_Size));
	MemorySnapShot_Store(&RAM_Bank1_Size, sizeof(RAM_Bank1_Size));
	MemorySnapShot_Store(&MMU_Bank0_Size, sizeof(MMU_Bank0_Size));
	MemorySnapShot_Store(&MMU_Bank1_Size, sizeof(MMU_Bank1_Size));
	MemorySnapShot_Store(&MMU_Conf_Expected, sizeof(MMU_Conf_Expected));
	
	/* Only save/restore area of memory machine is set to, eg 1Mb */
	MemorySnapShot_Store(STRam, STRamEnd);

	/* And Cart/TOS/Hardware area */
	MemorySnapShot_Store(&RomMem[0xE00000], 0x200000);

	if ( !bSave )
		memory_map_Standard_RAM ( MMU_Bank0_Size , MMU_Bank1_Size );
}


/**
 * Set default memory configuration, connected floppies, memory size and
 * clear the ST-RAM area.
 * As TOS checks hardware for memory size + connected devices on boot-up
 * we set these values ourselves and fill in the magic numbers so TOS
 * skips these tests.
 */
void STMemory_SetDefaultConfig(void)
{
	int i;
	int screensize, limit;
	int memtop, phystop;
	Uint8 MMU_Conf_Force;
	Uint8 nFalcSysCntrl;

	if (bRamTosImage)
	{
		/* Clear ST-RAM, excluding the RAM TOS image */
		STMemory_Clear(0x00000000, TosAddress);
		STMemory_Clear(TosAddress+TosSize, STRamEnd);
	}
	else
	{
		/* Clear whole ST-RAM */
		STMemory_Clear(0x00000000, STRamEnd);
	}

	/* Mirror ROM boot vectors */
	STMemory_WriteLong(0x00, STMemory_ReadLong(TosAddress));
	STMemory_WriteLong(0x04, STMemory_ReadLong(TosAddress+4));

	/* Fill in magic numbers to bypass TOS' memory tests for faster boot or
	 * if VDI resolution is enabled or if more than 4 MB of ram are used
	 * or if TT RAM added in Falcon mode.
	 * (for highest compatibility, those tests should not be bypassed in
	 *  the common STF/STE cases as some programs like "Yolanda" rely on
	 *  the RAM content after those tests) */
	if ( ConfigureParams.System.bFastBoot
	  || bUseVDIRes
	  || ( ConfigureParams.Memory.STRamSize_KB > (4*1024) && !bIsEmuTOS )
	  || ( Config_IsMachineTT() && ConfigureParams.System.bAddressSpace24 && !bIsEmuTOS )
	  || ( Config_IsMachineFalcon() && TTmemory && !bIsEmuTOS) )
	{
		/* Write magic values to sysvars to signal valid config */
		STMemory_WriteLong(0x420, 0x752019f3);    /* memvalid */
		STMemory_WriteLong(0x43a, 0x237698aa);    /* memval2 */
		STMemory_WriteLong(0x51a, 0x5555aaaa);    /* memval3 */

		/* If ST RAM detection is bypassed, we must also force TT RAM config if enabled */
		if ( TTmemory )
			STMemory_WriteLong ( 0x5a4 , 0x01000000 + TTmem_size );		/* ramtop */
		else
			STMemory_WriteLong ( 0x5a4 , 0 );		/* ramtop */
		STMemory_WriteLong ( 0x5a8 , 0x1357bd13 );		/* ramvalid */

		/* On Falcon, set bit6=1 at $ff8007 to simulate a warm start */
		/* (else memory detection is not skipped after a cold start/reset) */
		if (Config_IsMachineFalcon())
			STMemory_WriteByte ( 0xff8007, IoMem_ReadByte(0xff8007) | 0x40 );

		/* On TT, set bit0=1 at $ff8e09 to simulate a warm start */
		/* (else memory detection is not skipped after a cold start/reset) */
		if (Config_IsMachineTT())
			STMemory_WriteByte ( 0xff8e09, IoMem_ReadByte(0xff8e09) | 0x01 );

		/* TOS 3.0x and 4.0x check _hz200 and always do a memory test
		 * if the machine runs less than 80 seconds */
		if (!bIsEmuTOS && TosVersion >= 0x300)
			STMemory_WriteLong(0x4ba, 80 * 200);
	}

	/* Set memory size, adjust for extra VDI screens if needed. */
	screensize = VDIWidth * VDIHeight / 8 * VDIPlanes;
	/* Use 32 kiB in normal screen mode or when the screen size is smaller than 32 kiB */
	if (!bUseVDIRes || screensize < 0x8000)
		screensize = 0x8000;
	/* mem top - upper end of user memory (right before the screen memory)
	 * memtop / phystop must be dividable by 512 or TOS crashes */
	memtop = (STRamEnd - screensize) & 0xfffffe00;
	/* phys top - 32k gap causes least issues with apps & TOS
	 * as that's the largest _common_ screen size. EmuTOS behavior
	 * depends on machine type.
	 *
	 * TODO: what to do about _native_ TT & Videl resolutions
	 * which size is >32k?  Should memtop be adapted also for
	 * those?
	 */
	switch (ConfigureParams.System.nMachineType)
	{
	case MACHINE_FALCON:
		/* TOS v4 doesn't work with VDI mode (yet), and
		 * EmuTOS works with correct gap, so use that */
		phystop = STRamEnd;
		break;
	case MACHINE_TT:
		/* For correct TOS v3 memory detection, phystop should be
		 * at the end of memory, not at memtop + 32k.
		 *
		 * However:
		 * - TOS v3 crashes/hangs if phystop-memtop gap is larger
		 *   than largest real HW screen size (150k)
		 * - NVDI hangs if gap is larger than 32k in any other than
		 *   monochrome mode
		 */
		if (VDIPlanes == 1)
			limit = 1280*960/8;
		else
			limit = 0x8000;
		if (screensize > limit)
		{
			phystop = memtop + limit;
			fprintf(stderr, "WARNING: too large VDI mode for TOS v3 memory detection to work correctly!\n");
		}
		else
			phystop = STRamEnd;
		break;
	default:
		phystop = memtop + 0x8000;
	}
	STMemory_WriteLong(0x436, memtop);
	STMemory_WriteLong(0x42e, phystop);
	if (bUseVDIRes)
		fprintf(stderr, "VDI mode memtop: 0x%x, phystop: 0x%x (screensize: %d kB, memtop->phystop: %d kB)\n",
			memtop, phystop, (screensize+511) / 1024, (phystop-memtop+511) / 1024);

	/* If possible we don't override memory detection, TOS will do it
	 * (in that case MMU/MCU can be correctly emulated, and we do nothing
	 * and let TOS do its own memory tests using $FF8001) */
	if (!(Config_IsMachineST() || Config_IsMachineSTE())
	    || ConfigureParams.System.bFastBoot || bUseVDIRes
	    || ConfigureParams.Memory.STRamSize_KB > 4*1024)
	{
		/* Set memory controller byte according to different memory sizes */
		/* Setting per bank : %00=128k %01=512k %10=2Mb %11=reserved. - e.g. %1010 means 4Mb */
		if (ConfigureParams.Memory.STRamSize_KB <= 4*1024)
			MMU_Conf_Force = MMU_Conf_Expected;
		else
			MMU_Conf_Force = 0x0f;
		STMemory_WriteByte(0x424, MMU_Conf_Force);
		IoMem_WriteByte(0xff8001, MMU_Conf_Force);
	}

	if (Config_IsMachineFalcon())
	{
		/* Set the Falcon memory and monitor configuration register:

		         $ffff8006.b [R]  76543210  Monitor-memory
		                          ||||||||
		                          |||||||+- RAM Wait Status
		                          |||||||   0 =  1 Wait (default)
		                          |||||||   1 =  0 Wait
		                          ||||||+-- Video Bus size ???
		                          ||||||    0 = 16 Bit
		                          ||||||    1 = 32 Bit (default)
		                          ||||++--- ROM Wait Status
		                          ||||      00 = Reserved
		                          ||||      01 =  2 Wait (default)
		                          ||||      10 =  1 Wait
		                          ||||      11 =  0 Wait
		                          ||++----- Falcon Memory
		                          ||        00 =  1 MB
		                          ||        01 =  4 MB
		                          ||        10 = 14 MB
		                          ||        11 = no boot !
		                          ++------- Monitor-Typ
		                                    00 - Monochrome (SM124)
		                                    01 - Color (SC1224)
		                                    10 - VGA Color
		                                    11 - Television

		Bit 1 seems not to be well documented. It's used by TOS at bootup to compute the memory size.
		After some tests, I get the following RAM values (Bits 5, 4, 1 are involved) :

		00 =  512 Ko	20 = 8192 Ko
		02 = 1024 Ko	22 = 14366 Ko
		10 = 2048 Ko	30 = Illegal
		12 = 4096 Ko	32 = Illegal

		I use these values for Hatari's emulation.
		I also set the bit 3 and 2 at value 01 are mentioned in the register description.
		*/

		if (ConfigureParams.Memory.STRamSize_KB == 14*1024)	/* 14 Meg */
			nFalcSysCntrl = 0x26;
		else if (ConfigureParams.Memory.STRamSize_KB == 8*1024)	/* 8 Meg */
			nFalcSysCntrl = 0x24;
		else if (ConfigureParams.Memory.STRamSize_KB == 4*1024)	/* 4 Meg */
			nFalcSysCntrl = 0x16;
		else if (ConfigureParams.Memory.STRamSize_KB == 2*1024)	/* 2 Meg */
			nFalcSysCntrl = 0x14;
		else if (ConfigureParams.Memory.STRamSize_KB == 1024)	/* 1 Meg */
			nFalcSysCntrl = 0x06;
		else
			nFalcSysCntrl = 0x04;				/* 512 Ko */

		switch(ConfigureParams.Screen.nMonitorType) {
			case MONITOR_TYPE_TV:
				nFalcSysCntrl |= FALCON_MONITOR_TV;
				break;
			case MONITOR_TYPE_VGA:
				nFalcSysCntrl |= FALCON_MONITOR_VGA;
				break;
			case MONITOR_TYPE_RGB:
				nFalcSysCntrl |= FALCON_MONITOR_RGB;
				break;
			case MONITOR_TYPE_MONO:
				nFalcSysCntrl |= FALCON_MONITOR_MONO;
				break;
		}
		STMemory_WriteByte(0xff8006, nFalcSysCntrl);
	}

	/* Set TOS floppies */
	STMemory_WriteWord(0x446, nBootDrive);          /* Boot up on A(0) or C(2) */

	/* Create connected drives mask (only for harddrives, don't change floppy drive detected by TOS) */
	ConnectedDriveMask = STMemory_ReadLong(0x4c2);  // Get initial drive mask (see what TOS thinks)
	if (GEMDOS_EMU_ON)
	{
		for (i = 0; i < MAX_HARDDRIVES; i++)
		{
			if (emudrives[i] != NULL)     // Is this GEMDOS drive enabled?
				ConnectedDriveMask |= (1 << emudrives[i]->drive_number);
		}
	}
	/* Set connected drives system variable.
	 * NOTE: some TOS images overwrite this value, see 'OpCode_SysInit', too */
	STMemory_WriteLong(0x4c2, ConnectedDriveMask);
}


/**
 * Check that the region of 'size' starting at 'addr' is entirely inside
 * a memory bank of the same memory type
 */
bool	STMemory_CheckAreaType ( Uint32 addr , int size , int mem_type )
{
	addrbank	*pBank;

	pBank = &get_mem_bank ( addr );

	if ( ( pBank->flags & mem_type ) == 0 )
	{
		fprintf(stderr, "pBank flags mismatch: 0x%x & 0x%x (RAM = 0x%x)\n", pBank->flags, mem_type, ABFLAG_RAM);
		return false;
	}

	return pBank->check ( addr , size );
}


/**
 * Check if an address points to a memory region that causes bus error
 * This is used for blitter and other DMA chips that should not cause
 * a bus error when accessing such regions (on the contrary of the CPU)
 * Returns true if region gives bus error
 */
bool	STMemory_CheckRegionBusError ( Uint32 addr )
{
	return memory_region_bus_error ( addr );
}


/**
 * Convert an address in the ST memory space to a direct pointer
 * in the host memory.
 *
 * NOTE : Using this function to get a direct pointer to the memory should
 * only be used after doing a call to valid_address or STMemory_CheckAreaType
 * to ensure we don't try to access a non existing memory region.
 * Basically, this function should be used only for addr in RAM or in ROM
 */
void	*STMemory_STAddrToPointer ( Uint32 addr )
{
	Uint8	*p;

	if ( ConfigureParams.System.bAddressSpace24 == true )
		addr &= 0x00ffffff;			/* Only keep the 24 lowest bits */

	p = get_real_address ( addr );
	return (void *)p;
}



/**
 * Those functions are directly accessing the memory of the corresponding
 * bank, without calling its dedicated access handlers (they won't generate
 * bus errors or address errors or update IO values)
 * They are only used for internal work of the emulation, such as debugger,
 * log to print the content of memory, intercepting gemdos/bios calls, ...
 *
 * These functions are not used by the CPU emulation itself, see memory.c
 * for the functions that emulate real memory accesses.
 */

/**
 * Write long/word/byte into memory.
 * NOTE - value will be converted to 68000 endian
 */
void	STMemory_Write ( Uint32 addr , Uint32 val , int size )
{
	addrbank	*pBank;
	Uint8		*p;

//printf ( "mem direct write %x %x %d\n" , addr , val , size );
	pBank = &get_mem_bank ( addr );

	if ( pBank->baseaddr == NULL )
		return;					/* No real memory, do nothing */

	addr -= pBank->start & pBank->mask;
	addr &= pBank->mask;
	p = pBank->baseaddr + addr;

	/* We modify the memory, so we flush the instr/data caches if needed */
	M68000_Flush_All_Caches ( addr , size );
	
	if ( size == 4 )
		do_put_mem_long ( p , val );
	else if ( size == 2 )
		do_put_mem_word ( p , (Uint16)val );
	else
		*p = (Uint8)val;
}

void	STMemory_WriteLong ( Uint32 addr , Uint32 val )
{
	STMemory_Write ( addr , val , 4 );
}

void	STMemory_WriteWord ( Uint32 addr , Uint16 val )
{
	STMemory_Write ( addr , (Uint32)val , 2 );
}

void	STMemory_WriteByte ( Uint32 addr , Uint8 val )
{
	STMemory_Write ( addr , (Uint32)val , 1 );
}


/**
 * Read long/word/byte from memory.
 * NOTE - value will be converted to 68000 endian
 */
Uint32	STMemory_Read ( Uint32 addr , int size )
{
	addrbank	*pBank;
	Uint8		*p;

//printf ( "mem direct read %x %d\n" , addr , size );
	pBank = &get_mem_bank ( addr );

	if ( pBank->baseaddr == NULL )
		return 0;				/* No real memory, return 0 */

	addr -= pBank->start & pBank->mask;
	addr &= pBank->mask;
	p = pBank->baseaddr + addr;
	
	if ( size == 4 )
		return do_get_mem_long ( p );
	else if ( size == 2 )
		return (Uint32)do_get_mem_word ( p );
	else
		return (Uint32)*p;
}

Uint32	STMemory_ReadLong ( Uint32 addr )
{
	return (Uint32) STMemory_Read ( addr , 4 );
}

Uint16	STMemory_ReadWord ( Uint32 addr )
{
	return (Uint16)STMemory_Read ( addr , 2 );
}

Uint8	STMemory_ReadByte ( Uint32 addr )
{
	return (Uint8)STMemory_Read ( addr , 1 );
}




/*

Description of the MMU used in STF/STE to address RAM :
-------------------------------------------------------

Atari's computer used their own custom MMU to map logical addresses to physical RAM or to hardware registers.

The CAS/RAS mappings are based on Christian Zietz research to reverse the MMU's inner work, as well as by using
some custom programs on ST to change MMU configs and see how RAM content is modified when the shifter
displays it on screen.


When addressing RAM, the MMU will convert a logical address into the corresponding RAS0/CAS0L/CAS0H or
RAS1/CAS1L/CAS1H (using the MAD0-MAD9 signals), which will select the RAM chips needed to store the data.
Data are handled as 16 bits.

The mapping between a logical address and a physical bank/memory chips depends on the ST model.


STF :
  A bank is made of 16 chips of 1 bit memory. The MMU can use chips of 64 kbits, 256 kbits or 1024 kbits, which
  gives a bank size of 128 KB, 512 KB or 2048 KB (for example 16 chips of 41256 RAM will give 512 KB)

  Over the years, several revisions of the MMU were made :
   - C025912-20 : maker unknown, found in very first STs, banks 0 and 1 can be different
   - C025912-38 : made by Ricoh, found in most STFs, banks 0 and 1 can be different
   - C100109-001 : made by IMP, found in more recent STFs ; although different values can be set
     for banks 0 and 1, bank 0 setting will always apply to the 2 banks (so, 2.5 MB config is not possible)


STE :
  Each bank is made of 2 chips of SIMM RAM using 8 bit memory (instead of 1 bit on STF).

  The MMU was integrated into a bigger chip, the GST/MCU.
  As for the STF's IMP MMU, the MCU will only use bank 0 setting for both banks
    - C300589-001 : STE
    - C302183-001 : Mega STE


Regarding physical RAM on STF/STE, bank 1 can be empty or not, but bank 0 must always be filled (due to the way TOS
checks for available RAM and size, memory detection would give wrong results if bank 0 was empty and bank 1 was filled,
as bank 0 would be considered as 128 KB in such cases)


TT :
  The TT had several possibilities for memory extensions :
    - on board "slow" dual purpose (system/shifter) memory : 16 chips of 4 bit memory using 256 kbits or 1024 kbits modules
      Most (all ?) TT were shipped with 2 MB of on board RAM (ie 256 kbits chips).
      Using 1024 kbits chips, it's possible to get 8 MB of RAM
    - daughterboard "slow" dual purpose memory : similar to on board RAM, you get 2 MB or 8 MB
      - CA400313-xxx : 2 MB board by Atari
      - CA401059-xxx : 2 or 8 MB board by Atari
    - extension board using the VME BUS ; such RAM can't be used for shifter and it's slower than fast RAM
    - fast RAM : up to 256 MB of "fast" single purpose RAM could be added. It can't be used for shifter,
      but it can be used with TT DMA specific chips. As this RAM is not shared with the shifter, it's much faster
      (there's no bus cycle penalty every 250 ns as with dual purpose memory)

As tested by some people, if the TT has 8 MB on board and 8 MB on the daughterboard of "slow" dual purpose RAM,
then the resulting memory will be limited to 10 MB (addr 0x000000 to 0xA00000) and not to 14 or 16 MB,
the rest is reserved for cartridge, VME, ROM, IO regs


MMU configuration at $FF8001 :
  This register is used to specify the memory bank sizes used by the MMU to translate logical addresses
  into physical ones. Under normal operations, it should match the size of the physical RAM.

  STF/STE :  bits 2-3 = size of bank 0    bits 0-1 = size of bank 1
    bank size : 00 = 128 KB   01=512 KB   10=2048 KB   11=reserved

  TT : only bit 1 is used (there's only 1 bank)
    bank size : 0 = 2 MB (uses 256 kbits chips)   1 = 8 MB (uses 1024 kits chips)

*/




static void	STMemory_MMU_ConfToBank ( Uint8 MMU_conf , Uint32 *pBank0 , Uint32 *pBank1 )
{
	if ( Config_IsMachineTT() )
	{
		*pBank0 = STMemory_MMU_Size_TT ( ( MMU_conf >> 1 ) & 1  );
		*pBank1 = 0;
	}

	else
	{
		*pBank0 = STMemory_MMU_Size ( ( MMU_conf >> 2 ) & 3  );

		/* - STF with non-IMP MMU can have 2 different size of banks */
		/* - STF with IMP MMU and STE use bank0 value for the 2 banks (ie bank1=bank0 in all cases) */
		if ( Config_IsMachineST() )
			*pBank1 = STMemory_MMU_Size ( MMU_conf & 3  );
		else
			*pBank1 = MMU_Bank0_Size;
	}
}




/**
 * Return the number of bytes for a given MMU bank configuration on STF/STE
 * Possible values are 00, 01 or 10
 */
static int	STMemory_MMU_Size ( Uint8 MMU_conf )
{
	if ( MMU_conf == 0 )		return MEM_BANK_SIZE_128;
	else if ( MMU_conf == 1 )	return MEM_BANK_SIZE_512;
	else if ( MMU_conf == 2 )	return MEM_BANK_SIZE_2048;
	else				return 0;			/* invalid */
}




/**
 * Return the number of bytes for a given MMU bank configuration on TT
 * Possible values are 0 or 1
 */
static int	STMemory_MMU_Size_TT ( Uint8 MMU_conf )
{
	if ( MMU_conf == 0 )		return MEM_BANK_SIZE_2048;
	else				return MEM_BANK_SIZE_8192;
}




/**
 * Read the MMU banks configuration at $FF80001
 */
void	STMemory_MMU_Config_ReadByte ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	LOG_TRACE(TRACE_MEM, "mmu read memory config ff8001 val=0x%02x mmu_bank0=%d KB mmu_bank1=%d KB VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoMem[ 0xff8001 ] , MMU_Bank0_Size/1024 , MMU_Bank1_Size/1024 ,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
}




/**
 * Write to the MMU banks configuration at $FF80001
 * When value is changed, we remap the RAM bank into our STRam[] buffer
 * and enable addresses translation if necessary
 */
void	STMemory_MMU_Config_WriteByte ( void )
{
	int FrameCycles, HblCounterVideo, LineCycles;

	Video_GetPosition ( &FrameCycles , &HblCounterVideo , &LineCycles );

	STMemory_MMU_ConfToBank ( IoMem[ 0xff8001 ] , &MMU_Bank0_Size , &MMU_Bank1_Size );

	memory_map_Standard_RAM ( MMU_Bank0_Size , MMU_Bank1_Size );

	LOG_TRACE(TRACE_MEM, "mmu write memory config ff8001 val=0x%02x mmu_bank0=%d KB mmu_bank1=%d KB VBL=%d video_cyc=%d %d@%d pc=%x\n" ,
		IoMem[ 0xff8001 ] , MMU_Bank0_Size/1024 , MMU_Bank1_Size/1024 ,
		nVBLs , FrameCycles, LineCycles, HblCounterVideo , M68000_GetPC() );
}




/**
 * Check if "TotalMem" bytes is a valid value for the ST RAM size
 * and return the corresponding number of KB.
 * TotalMem can be expressed in MB if <= 14, else in KB
 * We list the most usual sizes, some more could be added if needed
 * Some values are not standard for all machines and will also require
 * to patch TOS to bypass RAM detection.
 *
 * If TotalMem is not a valid ST RAM size, return -1
 */
int	STMemory_RAM_Validate_Size_KB ( int TotalMem )
{
	/* Old format where ST RAM size was in MB between 0 and 14 */
	if ( TotalMem == 0 )
		return 512 * 1024;
	else if ( TotalMem <= 14 )
		return TotalMem * 1024;

	/* New format where ST RAM size is in KB */
	else if (  ( TotalMem ==  128 ) || ( TotalMem ==  256 ) || ( TotalMem ==  512 ) || ( TotalMem ==  640 )
		|| ( TotalMem == 1024 ) || ( TotalMem == 2048 ) || ( TotalMem == 2176 ) || ( TotalMem == 2560 )
		|| ( TotalMem == 4096 )	|| ( TotalMem == 8*1024 ) || ( TotalMem == 14*1024 ) )
		return TotalMem;

	return -1;
}




/**
 * For TotalMem <= 4MB, set the corresponding size in bytes for RAM bank 0 and RAM bank 1.
 * Also set the corresponding MMU value to expect at $FF8001
 * Return true if TotalMem is a valid ST RAM size for the MMU, else false
 */
bool	STMemory_RAM_SetBankSize ( int TotalMem , Uint32 *pBank0_Size , Uint32 *pBank1_Size , Uint8 *pMMU_Conf )
{
	int	TotalMem_KB = TotalMem / 1024;

	/* Check some possible RAM size configurations in KB */
	if ( TotalMem_KB == 128 )	{ *pBank0_Size =  128; *pBank1_Size =    0; *pMMU_Conf = (0<<2) + 0; }	/* 0x0 :  128 +    0 */
	else if ( TotalMem_KB == 256 )	{ *pBank0_Size =  128; *pBank1_Size =  128; *pMMU_Conf = (0<<2) + 0; }	/* 0x0 :  128 +  128 */
	else if ( TotalMem_KB == 512 )	{ *pBank0_Size =  512; *pBank1_Size =    0; *pMMU_Conf = (1<<2) + 0; }	/* 0x4 :  512 +    0 */
	else if ( TotalMem_KB == 640 )	{ *pBank0_Size =  512; *pBank1_Size =  128; *pMMU_Conf = (1<<2) + 0; }	/* 0x4 :  512 +  128 */
	else if ( TotalMem_KB == 1024 )	{ *pBank0_Size =  512; *pBank1_Size =  512; *pMMU_Conf = (1<<2) + 1; }	/* 0x5 :  512 +  512 */
	else if ( TotalMem_KB == 2048 )	{ *pBank0_Size = 2048; *pBank1_Size =    0; *pMMU_Conf = (2<<2) + 0; }	/* 0x8 : 2048 +    0 */
	else if ( TotalMem_KB == 2176 )	{ *pBank0_Size = 2048; *pBank1_Size =  128; *pMMU_Conf = (2<<2) + 0; }	/* 0x8 : 2048 +  128 */
	else if ( TotalMem_KB == 2560 )	{ *pBank0_Size = 2048; *pBank1_Size =  512; *pMMU_Conf = (2<<2) + 1; }	/* 0x9 : 2048 +  512 */
	else if ( TotalMem_KB == 4096 )	{ *pBank0_Size = 2048; *pBank1_Size = 2048; *pMMU_Conf = (2<<2) + 2; }	/* 0xA : 2048 + 2048 */

	else
	{
		fprintf ( stderr , "Invalid RAM size %d KB for MMU banks\n", TotalMem_KB );
		return false;
	}

fprintf ( stderr , "STMemory_RAM_SetBankSize total=%d KB bank0=%d KB bank1=%d KB MMU=%x\n" , TotalMem_KB, *pBank0_Size, *pBank1_Size, *pMMU_Conf );
	*pBank0_Size *= 1024;
	*pBank1_Size *= 1024;
	return true;
}




/**
 * STF : translate a logical address (as used by the CPU, DMA or the shifter) into a physical inside
 * the corresponding RAM bank using the RAS/CAS signal.
 * The STF MMU maps a 21 bit address (bits A20 .. A0) as follow :
 *  - A0 : used to select low/high byte of a 16 bit word
 *  - A1 ... A10 -> RAS0 ... RAS9
 *  - CASx :
 *     - if MMU set to 2 MB, then   A11 ... A20 -> CAS0 ... CAS9
 *     - if MMU set to 512 KB, then A10 ... A18 -> CAS0 ... CAS8
 *     - if MMU set to 128 KB, then  A9 ... A16 -> CAS0 ... CAS7
 *
 * [NP] As seen on a real STF (and confirmed by analyzing the STF's MMU), there's a special case
 * when bank0 is set to 128 KB and bank 1 is set to 2048 KB : the region between $40000 and $80000 will
 * not be mapped to any RAM at all, but will point to a "void" region ; this looks like a bug in the MMU's logic,
 * maybe not handled by Atari because this bank combination is unlikely to be used in real machines.
 */
static Uint32	STMemory_MMU_Translate_Addr_STF ( Uint32 addr_logical , int RAM_Bank_Size , int MMU_Bank_Size )
{
	Uint32	addr;


	if ( RAM_Bank_Size == MEM_BANK_SIZE_2048 )
	{
		/* RAM modules use lines MAD0-MAD9, C9/C8/R9/R8 exist : 21 bits per address in bank */
		if ( MMU_Bank_Size == MEM_BANK_SIZE_2048 )
		{
			/* 21 bit address is mapped to 21 bits : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   C   C   C   C   C   C   C   C   C   C   R  R  R  R  R  R  R  R  R  R  X */
			/*   9   8   7   6   5   4   3   2   1   0   9  8  7  6  5  4  3  2  1  0  X */
			addr = addr_logical;
		}
		else if ( MMU_Bank_Size == MEM_BANK_SIZE_512 )
		{
			/* 21 bit address is mapped to 19 bits (C9/R9 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   C   C   C   C   C   C   C   C   C  R  R  R  R  R  R  R  R  R  X */
			/*   .   .   8   7   6   5   4   3   2   1   0  8  7  6  5  4  3  2  1  0  X */
			addr = ( ( addr_logical & 0xffc00 ) << 1 )
				| ( addr_logical & 0x7ff );					/* add C9=A19 and R9=A10 */
		}
		else	/* if ( MMU_Bank_Size == MEM_BANK_SIZE_128 ) */
		{
			/* 21 bit address is mapped to 17 bits (C9/C8/R9/R8 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   .   .   C   C   C   C   C   C   C  C  R  R  R  R  R  R  R  R  X */
			/*   .   .   .   .   7   6   5   4   3   2   1  0  7  6  5  4  3  2  1  0  X */
			addr = ( ( addr_logical & 0x7fe00 ) << 2 )
				| ( addr_logical & 0x7ff );					/* add C9=A18 C8=A17 and R9=A10 R8=A9 */
		}
	}

	else if ( RAM_Bank_Size == MEM_BANK_SIZE_512 )
	{
		/* RAM modules use lines MAD0-MAD8, C9/R9 don't exist, C8/R8 exist : 19 bits per address in bank */
		if ( MMU_Bank_Size == MEM_BANK_SIZE_2048 )
		{
			/* 21 bit address is mapped to 21 bits : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   C   C   C   C   C   C   C   C   C   C   R  R  R  R  R  R  R  R  R  R  X */
			/*   9   8   7   6   5   4   3   2   1   0   9  8  7  6  5  4  3  2  1  0  X */
			addr = ( ( addr_logical & 0xff800 ) >> 1 ) | ( addr_logical & 0x3ff );  /* remove C9/R9 */
		}
		else if ( MMU_Bank_Size == MEM_BANK_SIZE_512 )
		{
			/* 21 bit address is mapped to 19 bits (C9/R9 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   C   C   C   C   C   C   C   C   C  R  R  R  R  R  R  R  R  R  X */
			/*   .   .   8   7   6   5   4   3   2   1   0  8  7  6  5  4  3  2  1  0  X */
			addr = addr_logical;
		}
		else	/* if ( MMU_Bank_Size == MEM_BANK_SIZE_128 ) */
		{
			/* 21 bit address is mapped to 17 bits (C9/C8/R9/R8 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   .   .   C   C   C   C   C   C   C  C  R  R  R  R  R  R  R  R  X */
			/*   .   .   .   .   7   6   5   4   3   2   1  0  7  6  5  4  3  2  1  0  X */
			addr = ( ( addr_logical & 0x3fe00 ) << 1 )
				| ( addr_logical & 0x3ff );					/* add C8=A17 and R8=A9 */
		}
	}

	else 	/* ( RAM_Bank_Size == MEM_BANK_SIZE_128 ) */
	{
		/* RAM modules use lines MAD0-MAD7, C9/C8/R9/R8 don't exist : 17 bits per address in bank */
		if ( MMU_Bank_Size == MEM_BANK_SIZE_2048 )
		{
			/* 21 bit address is mapped to 21 bits : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   C   C   C   C   C   C   C   C   C   C   R  R  R  R  R  R  R  R  R  R  X */
			/*   9   8   7   6   5   4   3   2   1   0   9  8  7  6  5  4  3  2  1  0  X */
			addr = ( ( addr_logical & 0x7f800 ) >> 2 ) | ( addr_logical & 0x1ff );  /* remove C9/C8/R9/R8 */
		}
		else if ( MMU_Bank_Size == MEM_BANK_SIZE_512 )
		{
			/* 21 bit address is mapped to 19 bits (C9/R9 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   C   C   C   C   C   C   C   C   C  R  R  R  R  R  R  R  R  R  X */
			/*   .   .   8   7   6   5   4   3   2   1   0  8  7  6  5  4  3  2  1  0  X */
			addr = ( ( addr_logical & 0x3fc00 ) >> 1 ) | ( addr_logical & 0x1ff );  /* remove C8/R8 */
		}
		else	/* if ( MMU_Bank_Size == MEM_BANK_SIZE_128 ) */
		{
			/* 21 bit address is mapped to 17 bits (C9/C8/R9/R8 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   .   .   C   C   C   C   C   C   C  C  R  R  R  R  R  R  R  R  X */
			/*   .   .   .   .   7   6   5   4   3   2   1  0  7  6  5  4  3  2  1  0  X */
			addr = addr_logical;
		}
	}


	addr &= ( RAM_Bank_Size - 1 );			/* Keep address inside RAM bank size */
	return addr;
}




/**
 * STE : translate a logical address (as used by the CPU, DMA or the shifter) into a physical inside
 * the corresponding RAM bank using the RAS/CAS signal.
 * The STE MMU maps a 21 bit address (bits A20 .. A0) as follow :
 *  - A0 : used to select low/high byte of a 16 bit word
 *  - A1 ... A20 -> RAS0 CAS0 RAS1 CAS1 ... RAS9 CAS9
 *
 * Note : the following code uses 9 cases for readability and to compare with STF, but it could be
 * largely reduced as many cases are common.
 */
static Uint32	STMemory_MMU_Translate_Addr_STE ( Uint32 addr_logical , int RAM_Bank_Size , int MMU_Bank_Size )
{
	Uint32	addr;


	if ( RAM_Bank_Size == MEM_BANK_SIZE_2048 )
	{
		/* RAM modules use lines MAD0-MAD9, C9/C8/R9/R8 exist : 21 bits per address in bank */
		if ( MMU_Bank_Size == MEM_BANK_SIZE_2048 )
		{
			/* 21 bit address is mapped to 21 bits : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   C   R   C   R   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   9   9   8   8   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = addr_logical;
		}
		else if ( MMU_Bank_Size == MEM_BANK_SIZE_512 )
		{
			/* 21 bit address is mapped to 19 bits (C9/R9 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   C   R   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   .   .   8   8   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = ( addr_logical & 0x1fffff );					/* add C9=A20 and R9=A19 */
		}
		else	/* if ( MMU_Bank_Size == MEM_BANK_SIZE_128 ) */
		{
			/* 21 bit address is mapped to 17 bits (C9/C8/R9/R8 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   .   .   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   .   .   .   .   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = ( addr_logical & 0x1fffff );					/* add C9=A20 C8=A18 and R9=A19 R8=A17 */
		}
	}

	else if ( RAM_Bank_Size == MEM_BANK_SIZE_512 )
	{
		/* RAM modules use lines MAD0-MAD8, C9/R9 don't exist, C8/R8 exist : 19 bits per address in bank */
		if ( MMU_Bank_Size == MEM_BANK_SIZE_2048 )
		{
			/* 21 bit address is mapped to 21 bits : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   C   R   C   R   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   9   9   8   8   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = ( addr_logical & 0x7ffff );					/* remove C9/R9 */
		}
		else if ( MMU_Bank_Size == MEM_BANK_SIZE_512 )
		{
			/* 21 bit address is mapped to 19 bits (C9/R9 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   C   R   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   .   .   8   8   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = addr_logical;
		}
		else	/* if ( MMU_Bank_Size == MEM_BANK_SIZE_128 ) */
		{
			/* 21 bit address is mapped to 17 bits (C9/C8/R9/R8 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   .   .   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   .   .   .   .   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = ( addr_logical & 0x7ffff );					/* add C8=A18 and R8=A17 */
		}
	}

	else 	/* ( RAM_Bank_Size == MEM_BANK_SIZE_128 ) */
	{
		/* RAM modules use lines MAD0-MAD7, C9/C8/R9/R8 don't exist : 17 bits per address in bank */
		if ( MMU_Bank_Size == MEM_BANK_SIZE_2048 )
		{
			/* 21 bit address is mapped to 21 bits : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   C   R   C   R   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   9   9   8   8   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = ( addr_logical & 0x1ffff );					/* remove C9/C8/R9/R8 */
		}
		else if ( MMU_Bank_Size == MEM_BANK_SIZE_512 )
		{
			/* 21 bit address is mapped to 19 bits (C9/R9 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   C   R   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   .   .   8   8   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = ( addr_logical & 0x1ffff );					/* remove C8/R8 */
		}
		else	/* if ( MMU_Bank_Size == MEM_BANK_SIZE_128 ) */
		{
			/* 21 bit address is mapped to 17 bits (C9/C8/R9/R8 are not used) : */
			/* a20 a19 a18 a17 a16 a15 a14 a13 a12 a11 a10 a9 a8 a7 a6 a5 a4 a3 a2 a1 a0 */
			/*   .   .   .   .   C   R   C   R   C   R   C  R  C  R  C  R  C  R  C  R  X */
			/*   .   .   .   .   7   7   6   6   5   5   4  4  3  3  2  2  1  1  0  0  X */
			addr = addr_logical;
		}
	}


	addr &= ( RAM_Bank_Size - 1 );			/* Keep address inside RAM bank size */
	return addr;
}




/**
 * Translate a logical address into a physical address inside the STRam[] buffer
 * by taking into account the size of the 2 MMU banks and the machine type (STF or STE)
 */
Uint32	STMemory_MMU_Translate_Addr ( Uint32 addr_logical )
{
	Uint32	addr;
	Uint32	addr_physical;
	Uint32	Bank_Start_physical;
	int	RAM_Bank_Size , MMU_Bank_Size;


	/* MMU only translates RAM addr < 4 MB */
	/* If logical address is beyond total MMU size and < 4MB, then we don't translate either */
	/* Useless check below : memory_map_Standard_RAM() ensures addr_logical is always < MMU_Bank0_Size + MMU_Bank1_Size when MMU is enabled */
// 	if ( addr_logical >= MMU_Bank0_Size + MMU_Bank1_Size )
// 		return addr_logical;

	addr = addr_logical;

	if ( addr < MMU_Bank0_Size )		/* Accessing bank0 */
	{
		Bank_Start_physical = 0;		/* Bank0's start relative to STRam[] */
		RAM_Bank_Size = RAM_Bank0_Size;		/* Physical size for bank0 */
		MMU_Bank_Size = MMU_Bank0_Size;		/* Logical size for bank0 */
	}
	else						/* Accessing bank1 */
	{
		Bank_Start_physical = RAM_Bank0_Size;	/* Bank1's start relative to STRam[] */
		RAM_Bank_Size = RAM_Bank1_Size;		/* Physical size for bank1 */
		MMU_Bank_Size = MMU_Bank1_Size;		/* Logical size for bank1 */
	}


	if ( Config_IsMachineST() )			/* For STF / Mega STF */
		addr_physical = STMemory_MMU_Translate_Addr_STF ( addr , RAM_Bank_Size , MMU_Bank_Size );
	else						/* For STE / Mega STE */
		addr_physical = STMemory_MMU_Translate_Addr_STE ( addr , RAM_Bank_Size , MMU_Bank_Size );

	addr_physical += Bank_Start_physical;

//fprintf ( stderr , "mmu translate %x -> %x pc=%x\n" , addr_logical , addr_physical , M68000_GetPC() );
	return addr_physical;
}



