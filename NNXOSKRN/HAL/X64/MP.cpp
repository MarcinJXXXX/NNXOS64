#define NNX_ALLOC_DEBUG 1
#include <nnxalloc.h>
#include "MP.h"
#include "APIC.h"
#include "PIT.h"
#include <HAL/paging.h>
#include <device/fs/vfs.h>
#include <HAL/pcr.h>
#include <bugcheck.h>
#include <scheduler.h>

extern "C" {

	PVOID* ApStackPointerArray;

	PVOID MpPopulateApStartupCode()
	{
		VFS_FILE* apCodeFile;
		VFS* systemPartition;
		PVOID code = (PVOID) 0x0000;
		_AP_DATA* data = (_AP_DATA*) (((UINT64) code) + 0x800);
		systemPartition = VfsGetSystemVfs();

		apCodeFile = systemPartition->Functions.OpenFile(systemPartition, (char*)"EFI\\BOOT\\APSTART.BIN");
		if (!apCodeFile)
		{
			PrintT("Error loading file\n");
			while (1);
		}
		else
		{
			systemPartition->Functions.ReadFile(apCodeFile, 0x1000, code);
			systemPartition->Functions.CloseFile(apCodeFile);
		}

		data->ApCR3 = PagingGetAddressSpace();
		data->ApStackPointerArray = ApStackPointerArray;
		data->ApProcessorInit = ApProcessorInit;
		HalpStoreGdt(&data->ApGdtr);
		HalpStoreIdt(&data->ApIdtr);

		return code;
	}

	UINT32 debugColors[] = {
		0xFF000000,
		0xFF0000AA,
		0xFF00AA00,
		0xFF00AAAA,
		0xFFAA0000,
		0xFFAA00AA,
		0xFFAA5500,
		0xFFAAAAAA,
		0xFF555555,
		0xFF5555FF,
		0xFF55FF55,
		0xFF55FFFF,
		0xFFFF5555,
		0xFFFF55FF,
		0xFFFFFF55,
		0xFFFFFFFF
	};

	UINT32 color = 0;

	VOID MpInitialize()
	{
		UINT64 i, j;
		UINT8 currentLapicId;
		PVOID apData, apCode;
		NTSTATUS status;

		ApStackPointerArray = (PVOID*) NNXAllocatorAllocArray(ApicNumberOfCoresDetected, sizeof(*ApStackPointerArray));

		apCode = MpPopulateApStartupCode();
		apData = (VOID*) (((UINT64) apCode) + 0x800);

		currentLapicId = ApicGetCurrentLapicId();

		ApicClearError();
		for (i = 0; i < ApicNumberOfCoresDetected; i++)
		{
			if (ApicLocalApicIDs[i] == currentLapicId)
				continue;

			ApStackPointerArray[i] = (PVOID)((ULONG_PTR)PagingAllocatePageFromRange(PAGING_KERNEL_SPACE, PAGING_KERNEL_SPACE_END) + PAGE_SIZE);
			ApicInitIpi(ApicLocalApicIDs[i], 0x00);
			PitUniprocessorPollSleepMs(10);

			for (j = 0; j < 2; j++)
			{
				ApicClearError();

				if ((UINT64) apCode > UINT16_MAX)
					KeBugCheck(HAL_INITIALIZATION_FAILED);

				ApicStartupIpi(ApicLocalApicIDs[i], 0, (UINT16)(UINT64)apCode);
				PitUniprocessorPollSleepUs(200);
			}
		}

		status = PspInitializeCore(currentLapicId);
		if (status)
			KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0, 0, 0);
	}

	__declspec(align(64)) UINT64 DrawLock = 0;

	VOID ApProcessorInit(UINT8 lapicId)
	{
		NTSTATUS status;

		HalpSetupPcrForCurrentCpu(lapicId);
		ApicLocalApicInitializeCore();

		status = PspInitializeCore(lapicId);
		if (status)
			KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0, 0, 0);

		/*
			shouldn't get here anyway 
		*/
		KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, 0, 0, 0, 0);
	}
}