#include "nnxint.h"
#include "video/SimpleTextIO.h"
#include "memory/physical_allocator.h"
#include "memory/paging.h"
#include "HAL/GDT.h"
#include "HAL/IDT.h"
//#define BOCHS
void IntTestASM();

void IntTestC() {
	PrintT("Interrupt test.\n");
}

const char version[] = " 0.1";

#ifdef BOCHS
void main(){
#else
void main(int* framebuffer, int* framebufferEnd, UINT32 width, UINT32 height, void (*ExitBootServices)(void*, UINT64), void* imageHandle, UINT64 n, UINT8* nnxMMap, UINT64 nnxMMapSize, UINT64 memorySize) {
#endif
	#ifndef BOCHS

	ExitBootServices(imageHandle, n);

	GlobalPhysicalMemoryMap = nnxMMap;
	GlobalPhysicalMemoryMapSize = nnxMMapSize;

	for (int a = 128; a < 384; a++) {
		GlobalPhysicalMemoryMap[a] = 0;
	}

	MemorySize = memorySize;

	TextIOInitialize(framebuffer, framebufferEnd, width, height);
	TextIOClear();

	PrintT("Initializing memory");
	PagingInit();
	
	GDTR* gdtr = 0xc1000000;
	GDT* gdt = 0xc1000000 + sizeof(GDTR);
	IDTR* idtr = 0xc1001000;
	IDT* idt = 0xc1001000+sizeof(IDTR);
	PagingMapPage(gdtr, InternalAllocatePhysicalPage(), PAGE_PRESENT | PAGE_WRITE);
	PagingMapPage(idtr, InternalAllocatePhysicalPage(), PAGE_PRESENT | PAGE_WRITE);
	#else
	GDTR *gdtr = 0x120000;
	GDT* gdt = 0x120000 + sizeof(GDTR);
	IDTR *idtr = 0x122000;
	IDT* idt = 0x122000 + sizeof(IDTR);
	#endif

	gdtr->size = sizeof(GDTEntry) * 5 - 1;
	gdtr->offset = gdt;

	idtr->size = sizeof(IDTEntry) * 64 - 1;
	idtr->offset = idt;

	((UINT64*)gdt->entries)[0] = 0;		//NULL DESCRIPTOR

	gdt->entries[1].base0to15 = 0;		//CODE, RING 0 DESCRIPTOR
	gdt->entries[1].base16to23 = 0;
	gdt->entries[1].base24to31 = 0;
	gdt->entries[1].limit0to15 = 0xFFFF;
	gdt->entries[1].limit16to19 = 0xF;
	gdt->entries[1].flags = 0xa;
	gdt->entries[1].accessByte = 0x9a;

	gdt->entries[2].base0to15 = 0;		//DATA, RING 0 DESCRIPTOR
	gdt->entries[2].base16to23 = 0;
	gdt->entries[2].base24to31 = 0;
	gdt->entries[2].limit0to15 = 0xFFFF;
	gdt->entries[2].limit16to19 = 0xF;
	gdt->entries[2].flags = 0xc;
	gdt->entries[2].accessByte = 0x92;

	gdt->entries[3].base0to15 = 0;	//CODE, RING 3 DESCRIPTOR
	gdt->entries[3].base16to23 = 0;
	gdt->entries[3].base24to31 = 0;
	gdt->entries[3].limit0to15 = 0xFFFF;
	gdt->entries[3].limit16to19 = 0xF;
	gdt->entries[3].flags = 0xa;
	gdt->entries[3].accessByte = 0xfa;

	gdt->entries[4].base0to15 = 0;		//DATA, RING 3 DESCRIPTOR
	gdt->entries[4].base16to23 = 0;
	gdt->entries[4].base24to31 = 0;
	gdt->entries[4].limit0to15 = 0xFFFF;
	gdt->entries[4].limit16to19 = 0xF;
	gdt->entries[4].flags = 0xc;
	gdt->entries[4].accessByte = 0xf2;
	LoadGDT(gdtr);

	for (int a = 0; a < 64; a++) {
		idt->entries[a].selector = 0x8;
		idt->entries[a].offset0to15 = (UINT16)(((UINT64)IntTestASM) & 0xFFFF);
		idt->entries[a].offset16to31 = (UINT16)((((UINT64)IntTestASM) >> 16) & 0xFFFF);
		idt->entries[a].offset32to63 = (UINT32)((((UINT64)IntTestASM) >> 32)&0xFFFFFFFF);
		idt->entries[a].type = 0x8E;
		idt->entries[a].ist = 0;
	}

	LoadIDT(idtr);
	
	#ifndef BOCHS
	PrintT("NNXOSLDR.exe version %s\n",version);
	PrintT("Stage 2 loaded... %x %x %i\n", framebuffer, framebufferEnd, (((UINT64)framebufferEnd) - ((UINT64)framebuffer)) / 4096);

	PrintT("Memory map: ");
	TextIOSetColorInformation(0xffffffff, 0xff007f00, 1);
	PrintT(" FREE ");
	TextIOSetColorInformation(0xffffffff, 0xff7f0000, 1);
	PrintT(" USED ");
	TextIOSetColorInformation(0xff000000, 0xffAfAf00, 1);
	PrintT(" UTIL ");
	TextIOSetColorInformation(0xffffffff, 0, 1);

	drawMap();

	TextIOSetCursorPosition(0, 400);
	#endif
	ForceInterrupt(1);
	while (1);
}
