#include "scheduler.h"
#include <pool.h>
#include <HAL/paging.h>
#include <SimpleTextIo.h>
#include <MemoryOperations.h>
#include <HAL/physical_allocator.h>
#include <HAL/pcr.h>
#include "ntqueue.h"
#include <bugcheck.h>
#include <nnxlog.h>
#include <HAL/cpu.h>
#include <ob/object.h>
#include <io/apc.h>

KSPIN_LOCK DispatcherLock;

LIST_ENTRY ProcessListHead;
LIST_ENTRY ThreadListHead;

UCHAR PspForegroundQuantum[3] = { 0x06, 0x0C, 0x12 };
UCHAR PspPrioritySeparation = 2;
UINT PspCoresInitialized = 0;

const CHAR PspThrePoolTag[4] = "Thre";
const CHAR PspProcPoolTag[4] = "Proc";
POBJECT_TYPE PsProcessType = NULL;
POBJECT_TYPE PsThreadType = NULL;

extern ULONG_PTR KeMaximumIncrement;
extern ULONG_PTR KiCyclesPerClockQuantum;
extern ULONG_PTR KiCyclesPerQuantum;
extern UINT KeNumberOfProcessors;

VOID PspSetupThreadState(PKTASK_STATE pThreadState, BOOL IsKernel, ULONG_PTR EntryPoint, ULONG_PTR Userstack);
VOID HalpUpdateThreadKernelStack(PVOID kernelStack);
NTSTATUS PspCreateProcessInternal(PEPROCESS* ppProcess);
NTSTATUS PspCreateThreadInternal(PETHREAD* ppThread, PEPROCESS pParentProcess, BOOL IsKernel, ULONG_PTR EntryPoint);
__declspec(noreturn) VOID PspTestAsmUser();
__declspec(noreturn) VOID PspTestAsmUserEnd();
__declspec(noreturn) VOID PspTestAsmUser2();
__declspec(noreturn) VOID PspIdleThreadProcedure();
BOOL TestBit(ULONG_PTR Number, ULONG_PTR BitIndex);
ULONG_PTR ClearBit(ULONG_PTR Number, ULONG_PTR BitIndex);
ULONG_PTR SetBit(ULONG_PTR Number, ULONG_PTR BitIndex);
NTSTATUS PspProcessOnCreate(PVOID SelfObject, PVOID CreateData);
NTSTATUS PspProcessOnCreateNoDispatcher(PVOID SelfObject, PVOID CreateData);
NTSTATUS PspProcessOnDelete(PVOID SelfObject);
NTSTATUS PspThreadOnCreate(PVOID SelfObject, PVOID CreateData);
NTSTATUS PspThreadOnCreateNoDispatcher(PVOID SelfObject, PVOID CreateData);
NTSTATUS PspThreadOnDelete(PVOID SelfObject);

struct _READY_QUEUES
{
    /* a ready queue for each thread priority */
    KQUEUE ThreadReadyQueues[32];
    ULONG ThreadReadyQueuesSummary;
};

typedef struct _THREAD_ON_CREATE_DATA
{
    ULONG_PTR Entrypoint;
    BOOL IsKernel;
    PEPROCESS ParentProcess;
}THREAD_ON_CREATE_DATA, * PTHREAD_ON_CREATE_DATA;

typedef struct _KPROCESSOR_READY_QUEUES
{
    struct _READY_QUEUES;
    PEPROCESS IdleProcess;
    PETHREAD IdleThread;
}KCORE_SCHEDULER_DATA, *PKCORE_SCHEDULER_DATA;

typedef struct _KSHARED_READY_QUEUE
{
    struct _READY_QUEUES;
    KSPIN_LOCK Lock;
}KSHARED_READY_QUEUE, *PKSHARED_READY_QUEUE;

PKCORE_SCHEDULER_DATA   CoresSchedulerData = (PKCORE_SCHEDULER_DATA)NULL;
KSHARED_READY_QUEUE     PspSharedReadyQueue = { 0 };

/* clear the summary bit for this priority if there are none entries left */
inline VOID ClearSummaryBitIfNeccessary(PKQUEUE ThreadReadyQueues, PULONG Summary, UCHAR Priority)
{
    if (IsListEmpty(&ThreadReadyQueues[Priority].EntryListHead))
    {
        *Summary = (ULONG)ClearBit(
            *Summary,
            Priority
        );
    }
}

inline VOID SetSummaryBitIfNeccessary(PKQUEUE ThreadReadyQueues, PULONG Summary, UCHAR Priority)
{
    if (!IsListEmpty(&ThreadReadyQueues[Priority].EntryListHead))
    {
        *Summary = (ULONG)SetBit(
            *Summary,
            Priority
        );
    }
}

NTSTATUS PspInitializeScheduler()
{
    INT i;
    KIRQL irql;
    NTSTATUS status;

    KeInitializeSpinLock(&DispatcherLock);
    KeAcquireSpinLock(&DispatcherLock, &irql);

    status = ObCreateSchedulerTypes(
        &PsProcessType, 
        &PsThreadType
    );

    PsProcessType->OnCreate = PspProcessOnCreate;
    PsProcessType->OnDelete = PspProcessOnDelete;
    PsThreadType->OnCreate = PspThreadOnCreate;
    PsThreadType->OnDelete = PspThreadOnDelete;

    if (status != STATUS_SUCCESS)
    {
        KeReleaseSpinLock(&DispatcherLock, irql);
        return status;
    }

    if (CoresSchedulerData == NULL)
    {
        KeInitializeSpinLock(&PspSharedReadyQueue.Lock);

        InitializeListHead(&ThreadListHead);
        InitializeListHead(&ProcessListHead);

        CoresSchedulerData = (PKCORE_SCHEDULER_DATA)PagingAllocatePageBlockFromRange(
            (KeNumberOfProcessors * sizeof(KCORE_SCHEDULER_DATA) + PAGE_SIZE - 1) / PAGE_SIZE,
            PAGING_KERNEL_SPACE,
            PAGING_KERNEL_SPACE_END
        );

        if (CoresSchedulerData != NULL)
        {
            for (i = 0; i < 32; i++)
            {
                KeInitializeQueue(&PspSharedReadyQueue.ThreadReadyQueues[i], 0);
            }
        }
    }

    KeReleaseSpinLock(&DispatcherLock, irql);
    return (CoresSchedulerData == (PKCORE_SCHEDULER_DATA)NULL) ? (STATUS_NO_MEMORY) : (STATUS_SUCCESS);
}

PKTHREAD PspSelectNextReadyThread(UCHAR CoreNumber)
{
    INT priority;
    PKTHREAD result;
    PKCORE_SCHEDULER_DATA coreOwnData = &CoresSchedulerData[CoreNumber];

    PspManageSharedReadyQueue(CoreNumber);
    result = &coreOwnData->IdleThread->Tcb;

    /* start with the highest priority */
    for (priority = 31; priority >= 0; priority--)
    {
        if (TestBit(coreOwnData->ThreadReadyQueuesSummary, priority))
        {
            PLIST_ENTRY dequeuedEntry = (PLIST_ENTRY)RemoveHeadList(&coreOwnData->ThreadReadyQueues[priority].EntryListHead);
            result = (PKTHREAD)((ULONG_PTR)dequeuedEntry - FIELD_OFFSET(KTHREAD, ReadyQueueEntry));

            ClearSummaryBitIfNeccessary(coreOwnData->ThreadReadyQueues, &coreOwnData->ThreadReadyQueuesSummary, priority);
            break;
        }
    }

    /* if no process was found on queue(s), return the idle thread */
    return result;
}

NTSTATUS PspCreateIdleProcessForCore(PEPROCESS* outIdleProcess, PETHREAD* outIdleThread, UINT8 coreNumber) 
{
    NTSTATUS status;
    PEPROCESS pIdleProcess;
    PETHREAD pIdleThread;
    THREAD_ON_CREATE_DATA threadCreationData;

    /**
     * Since DispatcherLock is already held, we cannot call the ObjectManager to create the idle thread.
     * This isn't a problem, because the idle thread cannot be dereferenced or deleted, and as such
     * can be manually created, without deadlock due to trying to acquire a lock already held bu this
     * core. 
     */

    /* allocate memory for the process structure */
    pIdleProcess = ExAllocatePool(NonPagedPool, sizeof(*pIdleProcess));
    if (pIdleProcess == NULL)
        return STATUS_NO_MEMORY;

    /* initialize the process structure */
    status = PspProcessOnCreateNoDispatcher((PVOID)pIdleProcess, NULL);
    if (!NT_SUCCESS(status))
    {
        ExFreePool(pIdleProcess);
        return status;
    }
    pIdleProcess->Pcb.AffinityMask = (1ULL << (ULONG_PTR)coreNumber);

    /* allocate memory fot the thread structure */
    pIdleThread = ExAllocatePool(NonPagedPool, sizeof(*pIdleThread));
    if (pIdleThread == NULL)
    {
        ExFreePool(pIdleProcess);
        return STATUS_NO_MEMORY;
    }

    /** 
     * initialize the thread creation data structure, 
     * which is neccessary to initialize the thread 
     */
    threadCreationData.Entrypoint = (ULONG_PTR)PspIdleThreadProcedure;
    threadCreationData.IsKernel = TRUE;
    threadCreationData.ParentProcess = pIdleProcess;

    /* initialize the thread structure */
    status = PspThreadOnCreateNoDispatcher((PVOID)pIdleThread, &threadCreationData);
    if (!NT_SUCCESS(status))
    {
        ExFreePool(pIdleProcess);
        ExFreePool(pIdleThread);
        return status;
    }
    pIdleThread->Tcb.ThreadPriority = 0;

    PrintT("Core %i's idle thread %X\n", coreNumber, pIdleThread);

    /* add the idle process and the idle thread to their respective lists */
    InsertHeadList(&ProcessListHead, &pIdleProcess->Pcb.ProcessListEntry);
    InsertHeadList(&ThreadListHead, &pIdleThread->Tcb.ThreadListEntry);

    *outIdleProcess = pIdleProcess;
    *outIdleThread = pIdleThread;

    return STATUS_SUCCESS;
}

VOID PspInitializeCoreSchedulerData(UINT8 CoreNumber)
{
    INT i;
    PKCORE_SCHEDULER_DATA thiscoreSchedulerData = &CoresSchedulerData[CoreNumber];
    NTSTATUS status;

    thiscoreSchedulerData->ThreadReadyQueuesSummary = 0;

    for (i = 0; i < 32; i++)
    {
        KeInitializeQueue(&thiscoreSchedulerData->ThreadReadyQueues[i], 0);
    }

    status = PspCreateIdleProcessForCore(&thiscoreSchedulerData->IdleProcess, &thiscoreSchedulerData->IdleThread, CoreNumber);
    PrintT("PspCreateIdleProcessForCore status: %X\n", status);
    if (status != STATUS_SUCCESS)
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0, 0, 0);
}


#pragma warning(push)
NTSTATUS PspInitializeCore(UINT8 CoreNumber)
{
    KIRQL irql;
    PKPCR pPcr;
    PKPRCB pPrcb;
    NTSTATUS status = STATUS_SUCCESS;

    /* check if this is the first core to be initialized */
    /* if so, initialize the scheduler first */
    if ((PVOID)CoresSchedulerData == NULL)
    {
        status = PspInitializeScheduler();
        
        PrintT("Scheduler initialization status %X\n", status);
        if (status)
            return status;
    }

    KeAcquireSpinLock(&DispatcherLock, &irql);
    PspInitializeCoreSchedulerData(CoreNumber);

    pPcr = KeGetPcr();
    PrintT("PCR core %i : %X\n", CoreNumber, pPcr);
    pPrcb = pPcr->Prcb;
    KeAcquireSpinLockAtDpcLevel(&pPrcb->Lock);
#pragma warning(disable: 6011)
    pPrcb->IdleThread = &CoresSchedulerData[CoreNumber].IdleThread->Tcb;
    pPrcb->NextThread = pPrcb->IdleThread;
    pPrcb->CurrentThread = pPrcb->IdleThread;
    pPcr->CyclesLeft = (LONG_PTR)KiCyclesPerQuantum * 100;
    KeReleaseSpinLockFromDpcLevel(&pPrcb->Lock);

    PspCoresInitialized++;
    HalpUpdateThreadKernelStack((PVOID)((ULONG_PTR)pPrcb->IdleThread->KernelStackPointer + sizeof(KTASK_STATE)));
    PagingSetAddressSpace(pPrcb->IdleThread->Process->AddressSpacePhysicalPointer);
    pPrcb->IdleThread->ThreadState = THREAD_STATE_RUNNING;
    KeReleaseSpinLock(&DispatcherLock, irql);

    if (PspCoresInitialized == KeNumberOfProcessors)
    {
        NTSTATUS status;
        PETHREAD userThread1, userThread2;

        NTSTATUS ObpMpTest();
        VOID TestUserThread1();
        VOID TestUserThread2();

        status = ObpMpTest();

        PrintT("Created userthread %i %i\n", PspCoresInitialized, KeNumberOfProcessors);
        PspCreateThreadInternal(
            &userThread1,
            (PEPROCESS)pPrcb->IdleThread->Process,
            FALSE,
            (ULONG_PTR)TestUserThread1
        );

        PspCreateThreadInternal(
            &userThread2,
            (PEPROCESS)pPrcb->IdleThread->Process,
            FALSE,
            (ULONG_PTR)TestUserThread2
        );

        userThread1->Tcb.ThreadPriority = 1;
        userThread2->Tcb.ThreadPriority = 1;
        PspInsertIntoSharedQueue((PKTHREAD)userThread1);
        PspInsertIntoSharedQueue((PKTHREAD)userThread2);
    }

    PKTASK_STATE pTaskState = pPrcb->IdleThread->KernelStackPointer;
    PagingSetAddressSpace(pPrcb->IdleThread->Process->AddressSpacePhysicalPointer);
    DisableInterrupts();
    KeLowerIrql(0);
    PspSwitchContextTo64(pPrcb->IdleThread->KernelStackPointer);

    return STATUS_SUCCESS;
}
#pragma warning(pop)

VOID SetCR8(QWORD);
ULONG_PTR PspScheduleThread(ULONG_PTR stack)
{
    PKPCR pcr;
    KIRQL irql;

    PKSPIN_LOCK originalRunningProcessSpinlock;
    PKTHREAD nextThread;
    PKTHREAD originalRunningThread;
    PKPROCESS originalRunningProcess;
    UCHAR originalRunningThreadPriority;
    
    pcr = KeGetPcr();

    KeAcquireSpinLock(&DispatcherLock, &irql);
    KeAcquireSpinLockAtDpcLevel(&pcr->Prcb->Lock);

    originalRunningThread = pcr->Prcb->CurrentThread;
    if (originalRunningThread == NULL)
    {
        KeBugCheckEx(WORKER_THREAD_TEST_CONDITION, (ULONG_PTR)originalRunningThread, 0, 0, 0);
    }
    
    originalRunningThread->KernelStackPointer = (PVOID)stack;

    if (originalRunningThread->ThreadState != THREAD_STATE_RUNNING)
        pcr->CyclesLeft = 0;

    originalRunningProcess = originalRunningThread->Process;
    if (originalRunningProcess == NULL)
    {
        KeBugCheckEx(WORKER_THREAD_TEST_CONDITION, (ULONG_PTR)originalRunningThread, 0, 0, 0);
    }
    originalRunningProcessSpinlock = &originalRunningProcess->ProcessLock;
    KeAcquireSpinLockAtDpcLevel(originalRunningProcessSpinlock);

    originalRunningThreadPriority = originalRunningThread->ThreadPriority + originalRunningProcess->BasePriority;

    /* if no next thread has been selected or the currently selected thread is not ready */
    if (pcr->Prcb->IdleThread == pcr->Prcb->NextThread || 
        pcr->Prcb->NextThread->ThreadState != THREAD_STATE_READY)
    {

        /* select a new next thread */
        pcr->Prcb->NextThread = PspSelectNextReadyThread(pcr->Prcb->Number);
    }

    if (pcr->CyclesLeft < (LONG_PTR)KiCyclesPerQuantum)
    {
        /* If no next thread was found, or the next thread can't preempt the current one 
         * and the current thread is not waiting, reset the quantum for current thread. */
        if (((pcr->Prcb->NextThread == pcr->Prcb->IdleThread && pcr->Prcb->CurrentThread != pcr->Prcb->IdleThread) ||
            (originalRunningThreadPriority > pcr->Prcb->NextThread->ThreadPriority + pcr->Prcb->NextThread->Process->BasePriority)) &&
            pcr->Prcb->CurrentThread->ThreadState == THREAD_STATE_RUNNING)
        {
            pcr->CyclesLeft = originalRunningProcess->QuantumReset * KiCyclesPerQuantum;
        }
        else
        {

            /* switch to next thread */
            nextThread = pcr->Prcb->NextThread;
            pcr->CyclesLeft = nextThread->Process->QuantumReset * KiCyclesPerQuantum;

            /* if thread was found */
            if (nextThread != originalRunningThread)
            {
                if (pcr->Prcb->CurrentThread->Process != pcr->Prcb->NextThread->Process)
                {
                    PagingSetAddressSpace((ULONG_PTR)pcr->Prcb->CurrentThread->Process->AddressSpacePhysicalPointer);
                }
            }

            nextThread->ThreadState = THREAD_STATE_RUNNING;

            /* select thread before setting the original one to ready */
            pcr->Prcb->NextThread = PspSelectNextReadyThread(pcr->Prcb->Number);
            pcr->Prcb->CurrentThread = nextThread;

            /* thread could have volountarily given up control, 
             * it could be waiting - then its state shouldn't be changed */
            if (originalRunningThread->ThreadState == THREAD_STATE_RUNNING)
            {
                PspInsertIntoSharedQueue(originalRunningThread);
                originalRunningThread->ThreadState = THREAD_STATE_READY;
            }
        }
    }
    else
    {
        pcr->CyclesLeft -= KiCyclesPerQuantum;

        if (pcr->CyclesLeft < 0)
        {
            pcr->CyclesLeft = 0;
        }
    }

    KeReleaseSpinLockFromDpcLevel(originalRunningProcessSpinlock);
    KeReleaseSpinLockFromDpcLevel(&pcr->Prcb->Lock);
    KeReleaseSpinLock(&DispatcherLock, irql);

    if (pcr->Prcb->CurrentThread->ApcState.KernelApcPending ||
        pcr->Prcb->CurrentThread->ApcState.UserApcPending)
    {
        KeDeliverApcs(
            PsGetProcessorModeFromTrapFrame(
                pcr->Prcb->CurrentThread->KernelStackPointer
            )
        );
    }
   
    return (ULONG_PTR)pcr->Prcb->CurrentThread->KernelStackPointer;
}

PKTHREAD PspGetCurrentThread()
{
    PKPCR pcr = KeGetPcr();
    return pcr->Prcb->CurrentThread;
}

PKTHREAD KeGetCurrentThread()
{
    return PspGetCurrentThread();
}

PVOID PspCreateKernelStack(SIZE_T nPages)
{
    return (PVOID)((ULONG_PTR)PagingAllocatePageBlockFromRange(
        nPages, 
        PAGING_KERNEL_SPACE, 
        PAGING_KERNEL_SPACE_END
    ) + PAGE_SIZE * nPages);
}

VOID
PspFreeKernelStack(
    PVOID OriginalStackLocation,
    SIZE_T nPages
)
{
    /* TODO */
}

ULONG_PTR GetRSP();

VOID 
PspSetupThreadState(
    PKTASK_STATE pThreadState, 
    BOOL IsKernel, 
    ULONG_PTR Entrypoint,
    ULONG_PTR Userstack
)
{
    UINT16 code0, code3, data0, data3;
    LPKGDTENTRY64 gdt;

    gdt = KeGetPcr()->Gdt;

    code0 = HalpGdtFindEntry(gdt, 7, TRUE, FALSE);
    code3 = HalpGdtFindEntry(gdt, 7, TRUE, TRUE);

    data0 = HalpGdtFindEntry(gdt, 7, FALSE, FALSE);
    data3 = HalpGdtFindEntry(gdt, 7, FALSE, TRUE);

    MemSet(pThreadState, 0, sizeof(*pThreadState));
    pThreadState->Rip = (UINT64)Entrypoint;
    pThreadState->Cs = IsKernel ? code0 : code3;
    pThreadState->Ds = IsKernel ? data0 : data3;
    pThreadState->Es = IsKernel ? data0 : data3;
    pThreadState->Fs = IsKernel ? data0 : data3;
    pThreadState->Gs = IsKernel ? data0 : data3;
    pThreadState->Ss = IsKernel ? data0 : data3;
    pThreadState->Rflags = 0x00000286;
    pThreadState->Rsp = Userstack;
}

NTSTATUS PspCreateProcessInternal(PEPROCESS* ppProcess)
{
    NTSTATUS status;
    PEPROCESS pProcess;
    OBJECT_ATTRIBUTES processObjAttributes;

    /* create process object */
    InitializeObjectAttributes(
        &processObjAttributes,
        NULL,
        OBJ_KERNEL_HANDLE,
        INVALID_HANDLE_VALUE,
        NULL
    );

    status = ObCreateObject(
        &pProcess, 
        0, 
        KernelMode, 
        &processObjAttributes, 
        sizeof(EPROCESS), 
        PsProcessType,
        NULL
    );

    if (status != STATUS_SUCCESS)
        return status;

    /* TODO:
     * Code above should be moved to some NtCreateProcess like function
     * code below should be run as a OnCreate method of the process type object 
     * (automatically by ObCreateObject).
     * Same thing goes for other thread/process creation/deletion functions */
    *ppProcess = pProcess;

    return STATUS_SUCCESS;
}


NTSTATUS PspProcessOnCreate(PVOID selfObject, PVOID createData)
{
    PEPROCESS pProcess;
    NTSTATUS status;
    KIRQL irql;

    pProcess = (PEPROCESS)selfObject;

    /* acquire the dispatcher lock */
    KeAcquireSpinLock(&DispatcherLock, &irql);

    /* initialize the dispatcher header */
    InitializeDispatcherHeader(&pProcess->Pcb.Header, OBJECT_TYPE_KPROCESS);

    /* initialize the process structure */
    status = PspProcessOnCreateNoDispatcher(selfObject, createData);
    /* add the process to the process list */
    InsertTailList(&ProcessListHead, &pProcess->Pcb.ProcessListEntry);

    /* release the dispatcher lock */
    KeReleaseSpinLock(&DispatcherLock, irql);

    return status;
}

NTSTATUS PspProcessOnCreateNoDispatcher(PVOID selfObject, PVOID createData)
{
    PEPROCESS pProcess = (PEPROCESS)selfObject;

    /* make sure it is not prematurely used */
    pProcess->Initialized = FALSE;
    KeInitializeSpinLock(&pProcess->Pcb.ProcessLock);
    KeInitializeSpinLock(&pProcess->Pcb.ProcessLock);
    InitializeListHead(&pProcess->Pcb.ThreadListHead);
    pProcess->Pcb.BasePriority = 0;
    pProcess->Pcb.AffinityMask = KAFFINITY_ALL;
    pProcess->Pcb.NumberOfThreads = 0;
    pProcess->Pcb.AddressSpacePhysicalPointer = PagingCreateAddressSpace();
    pProcess->Pcb.QuantumReset = 6;
    InitializeListHead(&pProcess->Pcb.HandleDatabaseHead);

    return STATUS_SUCCESS;
}

/**
 * @brief Allocates memory for a new thread, adds it to the scheduler's thread list and parent process' child thread list
 * @param ppThread pointer to a pointer to PETHREAD, value it's pointing to will be set to result of allocation after this function
 * @param pParentProcess pointer to EPROCESS structure of the parent process for this thread
 * @param IsKernel if true, thread is created in kernel mode
 * @param EntryPoint entrypoint function for the thread, caller is responsible for making any neccessary changes in the parent process' address space 
 * @return STATUS_SUCCESS, STATUS_NO_MEMORY
*/
NTSTATUS PspCreateThreadInternal(
    PETHREAD* ppThread, 
    PEPROCESS pParentProcess, 
    BOOL IsKernel, 
    ULONG_PTR EntryPoint
)
{
    NTSTATUS status;
    PETHREAD pThread;
    OBJECT_ATTRIBUTES threadObjAttributes;
    THREAD_ON_CREATE_DATA data;

    InitializeObjectAttributes(
        &threadObjAttributes,
        NULL,
        OBJ_KERNEL_HANDLE,
        INVALID_HANDLE_VALUE,
        NULL
    );

    data.Entrypoint = EntryPoint;
    data.ParentProcess = pParentProcess;
    data.IsKernel = IsKernel;

    status = ObCreateObject(
        &pThread,
        0,
        KernelMode,
        &threadObjAttributes,
        sizeof(ETHREAD),
        PsThreadType,
        &data
    );

    if (status != STATUS_SUCCESS)
        return status;

    *ppThread = pThread;

    return STATUS_SUCCESS;
}

/**
 * @brief This function is called by the object manager when a new thread
 * is being created. It initializes the dispatcher header of the thread,
 * the ETHREAD structure, adds the thread to parent process' child
 * list and adds the thread to the system wide thread list.
 * @param SelfObject pointer to Object Manager allocated object
 * @param CreateData pointer to THREAD_CREATE_DATA structure,
 * which holds the entrypoint and parent process pointer for example. 
 */
NTSTATUS PspThreadOnCreate(PVOID SelfObject, PVOID CreateData)
{
    KIRQL irql;
    NTSTATUS status;
    PETHREAD pThread;
    
    pThread = (PETHREAD)SelfObject;

    /* acquire the dispatcher lock */
    KeAcquireSpinLock(&DispatcherLock, &irql);

    /* initialize the dispatcher header */
    InitializeDispatcherHeader(&pThread->Tcb.Header, OBJECT_TYPE_KTHREAD);

    /* initialize the thread structure */
    status = PspThreadOnCreateNoDispatcher(SelfObject, CreateData);

    /**
     * initialize the parts of the thread structure 
     * that are "protected" by the dispatcher lock 
     */
    InsertTailList(&ThreadListHead, &pThread->Tcb.ThreadListEntry);
    KeInitializeApcState(&pThread->Tcb.ApcState);
    KeInitializeApcState(&pThread->Tcb.SavedApcState);
    pThread->Tcb.ThreadState = THREAD_STATE_READY;

    /* release the lock and return */
    KeReleaseSpinLock(&DispatcherLock, irql);
    return status;
}

NTSTATUS PspThreadOnCreateNoDispatcher(PVOID SelfObject, PVOID CreateData)
{
    KIRQL irql;
    ULONG_PTR userstack;
    ULONG_PTR originalAddressSpace;
    PTHREAD_ON_CREATE_DATA threadCreationData;
    PETHREAD pThread = (PETHREAD)SelfObject;
    threadCreationData = (PTHREAD_ON_CREATE_DATA)CreateData;

    if (threadCreationData == NULL)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&threadCreationData->ParentProcess->Pcb.ProcessLock, &irql);

    pThread->Tcb.ThreadState = THREAD_STATE_INITIALIZATION;
    pThread->Process = threadCreationData->ParentProcess;
    pThread->StartAddress = 0;
    KeInitializeSpinLock(&pThread->Tcb.ThreadLock);

    originalAddressSpace = PagingGetAddressSpace();
    PagingSetAddressSpace(pThread->Process->Pcb.AddressSpacePhysicalPointer);

    /* allocate stack even if in kernel mode */
    if (threadCreationData->IsKernel)
        userstack = (ULONG_PTR)PagingAllocatePageFromRange(PAGING_KERNEL_SPACE, PAGING_KERNEL_SPACE_END);
    else
        userstack = (ULONG_PTR)PagingAllocatePageFromRange(PAGING_USER_SPACE, PAGING_USER_SPACE_END);

    userstack += PAGE_SIZE;

    PagingSetAddressSpace(originalAddressSpace);

    MemSet(pThread->Tcb.ThreadWaitBlocks, 0, sizeof(pThread->Tcb.ThreadWaitBlocks));
    pThread->Tcb.ThreadPriority = 0;
    pThread->Tcb.NumberOfCurrentWaitBlocks = 0;
    pThread->Tcb.NumberOfActiveWaitBlocks = 0;
    pThread->Tcb.CurrentWaitBlocks = (PKWAIT_BLOCK)NULL;
    pThread->Tcb.WaitStatus = 0;
    pThread->Tcb.Alertable = FALSE;
    pThread->Tcb.Process = &pThread->Process->Pcb;
    pThread->Tcb.Timeout = 0;
    pThread->Tcb.TimeoutIsAbsolute = FALSE;

    /* Create stacks */
    /* Main kernel stack */
    pThread->Tcb.NumberOfKernelStackPages = 1;
    pThread->Tcb.OriginalKernelStackPointer = PspCreateKernelStack(pThread->Tcb.NumberOfKernelStackPages);
    pThread->Tcb.KernelStackPointer = (PVOID)((ULONG_PTR)pThread->Tcb.OriginalKernelStackPointer - sizeof(KTASK_STATE));
    /* Stack for saving the thread context when executing APCs */
    pThread->Tcb.ApcBackupKernelStackPointer = PspCreateKernelStack(1);

    /* Inherit affinity after the parent process */
    pThread->Tcb.Affinity = pThread->Tcb.Process->AffinityMask;
    PspSetupThreadState((PKTASK_STATE)pThread->Tcb.KernelStackPointer, threadCreationData->IsKernel, threadCreationData->Entrypoint, userstack);
    InsertTailList(&pThread->Process->Pcb.ThreadListHead, &pThread->Tcb.ProcessChildListEntry);
    KeReleaseSpinLock(&threadCreationData->ParentProcess->Pcb.ProcessLock, irql);

    return STATUS_SUCCESS;
}

NTSTATUS PspThreadOnDelete(PVOID selfObject)
{
    KIRQL irql;
    PETHREAD pThread = (PETHREAD)selfObject;

    KeAcquireSpinLock(&DispatcherLock, &irql);
    KeAcquireSpinLockAtDpcLevel(&pThread->Process->Pcb.ProcessLock);
    KeAcquireSpinLockAtDpcLevel(&pThread->Tcb.ThreadLock);

    pThread->Tcb.ThreadState = THREAD_STATE_TERMINATED;

    RemoveEntryList(&pThread->Tcb.ProcessChildListEntry);
    RemoveEntryList(&pThread->Tcb.ThreadListEntry);

    PspFreeKernelStack(
        pThread->Tcb.OriginalKernelStackPointer, 
        pThread->Tcb.NumberOfKernelStackPages
    );

    PspFreeKernelStack(
        pThread->Tcb.ApcBackupKernelStackPointer,
        1
    );

    KeReleaseSpinLockFromDpcLevel(&pThread->Tcb.ThreadLock);
    KeReleaseSpinLockFromDpcLevel(&pThread->Process->Pcb.ProcessLock);
    KeReleaseSpinLock(&DispatcherLock, irql);

    return STATUS_SUCCESS;
}

NTSTATUS PspProcessOnDelete(PVOID selfObject)
{
    KIRQL irql;
    PLIST_ENTRY current;
    PHANDLE_DATABASE currentHandleDatabase;
    PEPROCESS pProcess;

    pProcess = (PEPROCESS)selfObject;

    KeAcquireSpinLock(&DispatcherLock, &irql);
    KeAcquireSpinLockAtDpcLevel(&pProcess->Pcb.ProcessLock);

    current = pProcess->Pcb.ThreadListHead.First;
    while (current != &pProcess->Pcb.ThreadListHead)
    {
        /* the list of threads is not empty and somehow the process was dereferenced */
        KeBugCheckEx(CRITICAL_STRUCTURE_CORRUPTION, (ULONG_PTR)current, __LINE__, 0, 0);
    }

    currentHandleDatabase = (PHANDLE_DATABASE)pProcess->Pcb.HandleDatabaseHead.First;
    while (currentHandleDatabase != (PHANDLE_DATABASE)&pProcess->Pcb.HandleDatabaseHead)
    {
        PHANDLE_DATABASE next;
        INT i;

        next = (PHANDLE_DATABASE)currentHandleDatabase->HandleDatabaseChainEntry.Next;

        for (i = 0; i < ENTRIES_PER_HANDLE_DATABASE; i++)
        {
            ObCloseHandleByEntry(&currentHandleDatabase->Entries[i]);
        }

        ExFreePool(currentHandleDatabase);
        currentHandleDatabase = next;
    }

    RemoveEntryList(&pProcess->Pcb.ProcessListEntry);

    MmFreePfn(PFN_FROM_PA(pProcess->Pcb.AddressSpacePhysicalPointer));

    KeReleaseSpinLockFromDpcLevel(&pProcess->Pcb.ProcessLock);
    KeReleaseSpinLock(&DispatcherLock, irql);
    return STATUS_SUCCESS;
}

VOID PspInsertIntoSharedQueue(PKTHREAD Thread)
{
    UCHAR ThreadPriority;
    KIRQL irql;

    KeAcquireSpinLock(&PspSharedReadyQueue.Lock, &irql);

    ThreadPriority = (UCHAR)(Thread->ThreadPriority + (CHAR)Thread->Process->BasePriority);
    InsertHeadList(&PspSharedReadyQueue.ThreadReadyQueues[ThreadPriority].EntryListHead, (PLIST_ENTRY)&Thread->ReadyQueueEntry);
    PspSharedReadyQueue.ThreadReadyQueuesSummary = (ULONG)SetBit(
        PspSharedReadyQueue.ThreadReadyQueuesSummary,
        ThreadPriority
    );

    KeReleaseSpinLock(&PspSharedReadyQueue.Lock, irql);
}

BOOL PsCheckThreadIsReady(PKTHREAD Thread)
{
    KIRQL irql;
    BOOL ready;

    /* this function requires the DispatcherLock to be held */
    if (DispatcherLock == 0)
        KeBugCheckEx(SPIN_LOCK_NOT_OWNED, __LINE__, 0, 0, 0);

    KeAcquireSpinLock(&Thread->ThreadLock, &irql);

    if (Thread->NumberOfActiveWaitBlocks == 0 && Thread->ThreadState == THREAD_STATE_WAITING)
    {
        KeUnwaitThread(Thread, 0, 0);
    }

    /* 
        don't just set it to FALSE at the init and later change it to TRUE in the if block, 
        wouldn't work on a thread that's already ready 
    */
    ready = (Thread->ThreadState == THREAD_STATE_READY);

    KeReleaseSpinLock(&Thread->ThreadLock, irql);

    return ready;
}

BOOL PspManageSharedReadyQueue(UCHAR CoreNumber)
{
    PKCORE_SCHEDULER_DATA coreSchedulerData;
    KIRQL irql;
    INT checkedPriority;
    BOOL result;
    PKTHREAD thread;
    PKQUEUE sharedReadyQueues, coreReadyQueues;

    result = FALSE;
    coreSchedulerData = &CoresSchedulerData[CoreNumber];

    sharedReadyQueues = PspSharedReadyQueue.ThreadReadyQueues;
    coreReadyQueues = coreSchedulerData->ThreadReadyQueues;

    /* if there are no threads in the shared queue, don't bother with locking it and just return */
    if (PspSharedReadyQueue.ThreadReadyQueuesSummary == 0)
        return FALSE;

    KeAcquireSpinLock(&PspSharedReadyQueue.Lock, &irql);
    /* ugly 5 level nesting, but it works... or so i hope */

    /** 
     * the XOR between summaries gives us the bits in them that are different
     * if those bits are greater than coreSchedulerData's ready queue, it means 
     * that there was a higher bit in PspSharedReadyQueue's summary, which means
     * it has a thread with a higher priority ready
     */
    if (coreSchedulerData->ThreadReadyQueuesSummary <= (PspSharedReadyQueue.ThreadReadyQueuesSummary ^ coreSchedulerData->ThreadReadyQueuesSummary))
    {
        
        for (checkedPriority = 31; checkedPriority >= 0; checkedPriority--)
        {
            if (TestBit(PspSharedReadyQueue.ThreadReadyQueuesSummary, checkedPriority))
            {
                PLIST_ENTRY current = sharedReadyQueues[checkedPriority].EntryListHead.First;
                
                while (current != &sharedReadyQueues[checkedPriority].EntryListHead)
                {
                    thread = (PKTHREAD)((ULONG_PTR)current - FIELD_OFFSET(KTHREAD, ReadyQueueEntry));
                    
                    /* check if this processor can even run this thread */
                    if (thread->Affinity & (1LL << CoreNumber))
                    {
                        RemoveEntryList((PLIST_ENTRY)current);
                        InsertTailList(&coreReadyQueues[checkedPriority].EntryListHead, (PLIST_ENTRY)current);
                        result = TRUE;

                        SetSummaryBitIfNeccessary(coreReadyQueues, &coreSchedulerData->ThreadReadyQueuesSummary, checkedPriority);
                        ClearSummaryBitIfNeccessary(sharedReadyQueues, &PspSharedReadyQueue.ThreadReadyQueuesSummary, checkedPriority);

                        break;
                    }

                    current = current->Next;
                }
            }

            if (result)
                break;
        }
    }

    KeReleaseSpinLock(&PspSharedReadyQueue.Lock, irql);

    return result;
}

__declspec(noreturn) VOID PsExitThread(DWORD exitCode)
{
    PKTHREAD currentThread;
    KIRQL originalIrql;
    PKWAIT_BLOCK waitBlock;
    PLIST_ENTRY waitEntry;
    PKTHREAD waitingThread;
    KIRQL irql;

    /* raise irql as the CPU shouldn't try to schedule here */
    KeAcquireSpinLock(&DispatcherLock, &originalIrql);

    currentThread = KeGetCurrentThread();
    KeAcquireSpinLockAtDpcLevel(&currentThread->Header.Lock);
    KeAcquireSpinLockAtDpcLevel(&currentThread->ThreadLock);
    currentThread->ThreadState = THREAD_STATE_TERMINATED;
    currentThread->ThreadExitCode = exitCode;

    /* satisfy all waits for the thread */
    while (!IsListEmpty(&currentThread->Header.WaitHead))
    { 
        /* for each wait block */
        waitEntry = (PLIST_ENTRY)currentThread->Header.WaitHead.First;
        waitBlock = (PKWAIT_BLOCK)waitEntry;
        waitingThread = waitBlock->Thread;
        
        /* decrease the number of active wait blocks */
        KeAcquireSpinLockAtDpcLevel(&waitingThread->ThreadLock);

        waitingThread->NumberOfActiveWaitBlocks--;
        KeReleaseSpinLockFromDpcLevel(&waitingThread->ThreadLock);

        RemoveEntryList(waitEntry);
        MemSet(waitBlock, 0, sizeof(*waitBlock));

        /* check if the waiting thread has become ready */
        PsCheckThreadIsReady(waitingThread);
    }

    currentThread->Header.SignalState = INT32_MAX;
    KeReleaseSpinLockFromDpcLevel(&currentThread->Header.Lock);
    KeReleaseSpinLockFromDpcLevel(&currentThread->ThreadLock);
    KeReleaseSpinLockFromDpcLevel(&DispatcherLock);

    /* TODO: rewrite */
    /* interrupts are disabled, they will be reenabled by RFLAGS on the idle thread stack */
    /* IRQL can be lowered now */
    DisableInterrupts();
    KeLowerIrql(originalIrql);

    /* dereference the current thread, this could destroy it so caution is neccessary
     * it is impossible to run PspScheduleNext beacause it could try to save registers on the old kernel stack 
     * TODO: deal with this in a more civilized manner */
    ObDereferenceObject(currentThread);

    /* go to the idle thread, on the next tick it will be swapped to some other thread */
    /* TODO: do scheduling instead of switching to the idle thread */
    KeAcquireSpinLock(&KeGetPcr()->Prcb->Lock, &irql);
    currentThread = KeGetPcr()->Prcb->CurrentThread = KeGetPcr()->Prcb->IdleThread;
    KeReleaseSpinLock(&KeGetPcr()->Prcb->Lock, irql);
    PspSwitchContextTo64(currentThread->KernelStackPointer);
}

BOOL
KiSetUserMemory(
    PVOID Address,
    ULONG_PTR Data
)
{
    /* TODO: do checks before setting the data */
    *((ULONG_PTR*)Address) = Data;
    return TRUE;
}

/* TODO: implement a PspGetCallbackParameter maybe? */
VOID
PspSetUsercallParameter(
    PKTHREAD pThread,
    ULONG ParameterIndex,
    ULONG_PTR Value
)
{
#ifdef _M_AMD64
    PKTASK_STATE pTaskState;

    pTaskState = (PKTASK_STATE)pThread->KernelStackPointer;

    switch (ParameterIndex)
    {
    case 0:
        pTaskState->Rcx = Value;
        break;
    case 1:
        pTaskState->Rdx = Value;
        break;
    case 2:
        pTaskState->R8 = Value;
        break;
    case 3:
        pTaskState->R9 = Value;
        break;
    default:
    {
        /* This is called after allocating the shadow space and the return address */
        ULONG_PTR stackLocation = pTaskState->Rsp;

        /* skip the return address and shadow space */
        stackLocation += 4 * sizeof(ULONG_PTR) + 1 * sizeof(PVOID);

        /* parameter's relative stack location */
        stackLocation += sizeof(ULONG_PTR) * (ParameterIndex - 4);

        KiSetUserMemory((PVOID)stackLocation, Value);
        break;
    }
    }
#else
#error Unimplemented
#endif
}

VOID
PspUsercall(
    PKTHREAD pThread,
    PVOID Function,
    ULONG_PTR* Parameters,
    SIZE_T NumberOfParameters,
    PVOID ReturnAddress
)
{
#ifdef _M_AMD64
    PKTASK_STATE pTaskState;
    INT i;

    pTaskState = (PKTASK_STATE)pThread->KernelStackPointer;

    /* allocate stack space for the registers that don't fit onto the stack */
    if (NumberOfParameters > 4)
    {
        pTaskState->Rsp -= (NumberOfParameters - 4) * sizeof(ULONG_PTR);
    }

    /* allocate the shadow space */
    pTaskState->Rsp -= 4 * sizeof(ULONG_PTR);

    /* allocate the return address */
    pTaskState->Rsp -= sizeof(PVOID);
    KiSetUserMemory((PVOID)pTaskState->Rsp, (ULONG_PTR)ReturnAddress);

    for (i = 0; i < NumberOfParameters; i++)
    {
        PspSetUsercallParameter(pThread, i, Parameters[i]);
    }

#else
#error Unimplemented
#endif 
}

KPROCESSOR_MODE
PsGetProcessorModeFromTrapFrame(
    PKTASK_STATE TrapFrame
)
{
    if ((TrapFrame->Cs & 0x3) == 0x00)
        return KernelMode;
    return UserMode;
}

PKTHREAD
KiGetCurrentThreadLocked()
{
    PKTHREAD pThread;
    pThread = KeGetCurrentThread();
    KeAcquireSpinLockAtDpcLevel(&pThread->ThreadLock);
    return pThread;
}

VOID
KiUnlockThread(
    PKTHREAD pThread
)
{
    KeReleaseSpinLockFromDpcLevel(&pThread->ThreadLock);
}
