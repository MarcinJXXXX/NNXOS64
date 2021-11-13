#ifndef NNX_APIC_HEADER
#define NNX_APIC_HEADER


#include "../ACPI/AML.h"

#ifdef __cplusplus
#include "../ACPI/AMLCPP.h"
extern "C" {
#endif
	/* Functions accessible from both C and C++ */
	VOID ApicInit(ACPI_MADT* madt);
	VOID ApicLocalApicWriteRegister(UINT64 offset, UINT32 data);
	UINT32 ApicLocalApicReadRegister(UINT64 offset);
	VOID ApicClearError();
	VOID ApicSendIpi(UINT8 destination, UINT8 destinationShorthand, UINT8 deliveryMode, UINT8 vector);
	VOID ApicInitIpi(UINT8 destination, UINT8 destinationShorthand);
	VOID ApicStartupIpi(UINT8 destination, UINT8 destinationShorthand, UINT16 startupCode);
	VOID ApicLocalApicWriteRegister(UINT64 offset, UINT32 data);
	UINT32 ApicLocalApicReadRegister(UINT64 offset);
	UINT8 ApicGetCurrentLapicId();
	extern UINT64 ApicNumberOfCoresDetected;
	extern UINT8* ApicLocalApicIDs;
	extern UINT64 ApicVirtualLocalApicBase;
	extern UINT64 ApicLocalApicBase;
	VOID ApicLocalApicInitializeCore();
#ifdef __cplusplus
}

#endif

/* Constants */
#define LAPIC_ID_REGISTER_OFFSET					0x20
#define LAPIC_TASK_PRIORITY_REGISTER_OFFSET			0x80
#define LAPIC_EOI_REGISTER_OFFSET                   0xB0
#define LAPIC_SPURIOUS_INTERRUPT_REGISTER_OFFSET	0xF0
#define LAPIC_ERROR_REGISTER_OFFSET					0x280
#define	LAPIC_ICR1_REGISTER_OFFSET					0x300
#define LAPIC_ICR0_REGISTER_OFFSET					0x310
#define LAPIC_LVT_TIMER_REGISTER_OFFSET				0x320
#define LAPIC_LVT_LINT0_REGISTER_OFFSET				0x350
#define LAPIC_LVT_LINT1_REGISTER_OFFSET				0x360
#define LAPIC_LVT_ERROR_REGISTER_OFFSET				0x370
#define LAPIC_INITIAL_TIMER_COUNT_REGISTER_OFFSET	0x380
#define LAPIC_CURRENT_TIMER_COUNT_REGISTER_OFFSET	0x390
#define LAPIC_DIVIDE_TIMER_REGISTER_OFFSET			0x3E0

#define LAPIC_TIMER_MODE_PERIODIC 0x20000
#define LAPIC_TIMER_MODE_ONE_SHOT 0x00000
#define LAPIC_TIMER_DIVISOR_1     0x0B
#define LAPIC_TIMER_DIVISOR_2	  0x00
#define LAPIC_TIMER_DIVISOR_4     0x01
#define LAPIC_TIMER_DIVISOR_8	  0x02
#define LAPIC_TIMER_DIVISOR_16    0X03
#define LAPIC_TIMER_DIVISOR_32	  0x08
#define LAPIC_TIMER_DIVISOR_64	  0x09
#define LAPIC_TIMER_DIVISOR_128   0x0A
#endif