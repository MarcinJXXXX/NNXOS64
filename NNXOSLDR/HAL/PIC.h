#pragma once
#define PIC_PRIMARY 0x20
#define PIC_SECONDARY 0xA0

#define PIC_PRIMARY_DATA PIC_PRIMARY+1
#define PIC_SECONDARY_DATA PIC_SECONDARY+1

void PICInitialize();