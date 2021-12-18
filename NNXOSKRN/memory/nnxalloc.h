#ifndef NNX_ALLOC_HEADER
#define NNX_ALLOC_HEADER
#pragma pack(push, 1)

#include <nnxtype.h>

typedef struct MemoryBlock
{
    UINT64 size;
    struct MemoryBlock* next;
    UINT8 flags;
}MemoryBlock;

#pragma pack(pop)

#define MEMBLOCK_FREE 0
#define MEMBLOCK_USED 1

#ifdef __cplusplus
extern "C"
{
#endif
    VOID    NNXAllocatorInitialize();
    VOID    NNXAllocatorAppend(PVOID memblock, UINT64 sizeofMemblock);

    PVOID    NNXAllocatorAllocP(UINT64 size, BOOL debug, UINT64 line, const CHAR* function);
    PVOID    NNXAllocatorAllocArray(UINT64 n, UINT64 size);
    VOID    NNXAllocatorFreeP(PVOID address, BOOL debug, UINT64 line, const CHAR* function);

    UINT64    NNXAllocatorGetTotalMemory();
    UINT64    NNXAllocatorGetUsedMemory();
    UINT64    NNXAllocatorGetFreeMemory();
    UINT64    NNXAllocatorGetUsedMemoryInBlocks();
    VOID    NNXAllocatorDiagnostics(const char* message);


#ifndef NNX_ALLOC_DEBUG
#define NNX_ALLOC_DEBUG 0
#endif
#define NNXAllocatorAlloc(x) NNXAllocatorAllocP(x,NNX_ALLOC_DEBUG,__LINE__,__FUNCTION__)
#define NNXAllocatorFree(x) NNXAllocatorFreeP(x,NNX_ALLOC_DEBUG,__LINE__,__FUNCTION__)

#ifdef __cplusplus
}

#endif


#ifdef DEBUG

#define SaveStateOfMemory(c)\
        __caller = c;\
        __lastMemory = NNXAllocatorGetUsedMemoryInBlocks()

#define CheckMemory()\
        __currentMemory = NNXAllocatorGetUsedMemoryInBlocks();\
        if (__lastMemory < __currentMemory) {\
            PrintT("----------------\n");\
            if (__caller)\
                PrintT("%s: Potential memory leak of %i bytes\n", __caller, __currentMemory - __lastMemory);\
            PrintT("Total memory: %i, Used memory: %i, Free memory: %i\n", NNXAllocatorGetTotalMemory(), NNXAllocatorGetUsedMemory(), NNXAllocatorGetFreeMemory());\
        }\
        else if (__lastMemory > __currentMemory) {\
            PrintT("----------------\n");\
            if (__caller)\
                PrintT("%s: Somehow we have more memory.\n", __caller);\
            PrintT("Investigate... %i\n", __lastMemory - __currentMemory);\
        }\
        __caller = 0
#else 
#define SaveStateOfMemory(c)
#define CheckMemory()

#endif

#endif



