/*
LM3S3N26/Cygni Linker
teho Labs 2011
For use with the bootloader
See: http://sourceware.org/binutils/docs/ld/index.html
for docs and examples
*/

MEMORY
{
	FLASH (rx) : ORIGIN = 0x00001000, LENGTH = 64K
	SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 12K
}

SECTIONS
{
	/*All of the program code is in flash memory along with the interupt table and read only data*/	
	.text :
	{
		_text = .;		/*start text symbol*/
		KEEP(*(.isr_vector))
		*(.text*)
		*(.rodata*)
		_etext = .;		/*end of text symbol*/
	} >FLASH

	/*
	This is the initalized data section, it follows the .text section in the binary image
	The values are copied to RAM by using the _etext label above, and knowing the length via _edata below
	see ResetISR in startup_gcc.c 
	*/
	.data : AT(ADDR(.text) + SIZEOF(.text))
	{
		_data = .;		/*start of data symbol*/
		*(vtable)
		*(.data*)
		_edata = .;		/*end of data symbol*/
	} >SRAM

	/*This stores basically blank global vars (IE char global_buffer[100];) it is intialized to 0*/
	.bss :
	{
		_bss = .;		/*start bss data symbol*/
		*(.bss*)
		*(COMMON)
		_ebss = .;		/*end bss data symbol*/
	} >SRAM
}
