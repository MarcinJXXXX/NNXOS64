#pragma once
#include <nnxtype.h>
#include <irql.h>
#include <spinlock.h>
#include <ntlist.h>

#ifdef __cplusplus
extern "C" 
{
#endif

#ifdef NNX_KERNEL

    typedef BOOLEAN(NTAPI*KSERVICE_ROUTINE)
        (struct _KINTERRUPT* Interrupt, PVOID ServiceCtx);
    typedef KSERVICE_ROUTINE *PKSERVICE_ROUTINE;
    typedef ULONG_PTR(NTAPI*KFULLCTX_ROUTINE)
        (struct _KINTERRUPT* Interrupt, struct _KTASK_STATE*);

    typedef struct _KINTERRUPT
    {
        KSPIN_LOCK          Lock;
        KIRQL               InterruptIrql;
        BYTE                Handler[32];
        UCHAR               Vector;
        BOOLEAN             Connected;
        ULONG               CpuNumber;
        LIST_ENTRY          CpuListEntry;
        PVOID               ServiceCtx;
        KSERVICE_ROUTINE    pfnServiceRoutine;
        KFULLCTX_ROUTINE    pfnFullCtxRoutine;
        BOOLEAN             Trap;
        BOOLEAN             (NTAPI *pfnGetRequiresFullCtx)(
                                struct _KINTERRUPT* Interrupt);
        BOOLEAN             SendEOI;
        VOID                (NTAPI *pfnSetMask)(
                                struct _KINTERRUPT* Self, 
                                BOOLEAN State);
        /* FIXME */
        ULONG               IoApicVector;
        
    } KINTERRUPT, *PKINTERRUPT;

    BOOLEAN
    NTAPI 
    KeConnectInterrupt(
        PKINTERRUPT Interrupt);

    BOOLEAN  
    NTAPI 
    KeDisconnectInterrupt(
        PKINTERRUPT Interrupt);

    NTSTATUS 
    NTAPI 
    KiInitializeInterrupts(VOID);

    NTSTATUS 
    NTAPI 
    KiCreateInterrupt(
        PKINTERRUPT* ppInterrupt);
    
    VOID 
    EnableInterrupts();
    
    VOID 
    DisableInterrupts();

    NTSTATUS
    NTAPI
    IoCreateInterrupt(
        PKINTERRUPT* pOutInterrupt,
        UCHAR Vector,
        PVOID Handler,
        ULONG CpuNumber,
        KIRQL Irql,
        BOOL  Trap,
        KSERVICE_ROUTINE Routine);

    VOID 
    NTAPI 
    IrqHandler();

    #define CLOCK_VECTOR 0x20
    #define KEYBOARD_VECTOR 0x30
    #define STOP_IPI_VECTOR 0xEF

#endif

#ifdef __cplusplus
}
#endif