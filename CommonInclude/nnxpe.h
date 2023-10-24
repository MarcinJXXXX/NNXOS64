#ifndef _NNX_PE_H
#define _NNX_PE_H 

#pragma pack(push, 1)
#include <nnxtype.h>

#define IMAGE_PE_MAGIC 0x4550
#define IMAGE_MZ_MAGIC 0x5A4D
#define IMAGE_MACHINE_X32 0x14c
#define IMAGE_MACHINE_X64 0x8664
#define IMAGE_MACHINE_IA64 0x200
#define IMAGE_MACHINE_EFI 0xebc

typedef UINT32 RVA;
typedef UINT8 CHAR8;

typedef struct _MZ_FILE_TABLE 
{
    UINT16 Signature;
    UINT8 nc[58];
    UINT32 e_lfanew;
}IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

typedef struct _FILE_HEADER_TABLE 
{
    UINT16 Machine;
    UINT16 NumberOfSections;
    UINT32 TimeDateStamp;
    RVA SymbolicTableRVA;
    UINT32 SymbolCount;
    UINT16 OptionalHeaderSize;
    UINT16 Characteristics;
}IMAGE_COFF_HEADER, IMAGE_COFF_TABLE, IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

typedef struct _IMAGE_DATA_DIRECTORY
{
    RVA VirtualAddressRVA;
    UINT32 Size;
}IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

#define IMAGE_OPTIONAL_HEADER_NT64 0x20B
#define IMAGE_OPTIONAL_HEADER_NT32 0x10B
#define IMAGE_OPTIONAL_HEADER_ROM 0x107

#define IMAGE_SUBSYSTEM_NONE 0
#define IMAGE_SUBSYSTEM_NATIVE 1
#define IMAGE_SUBSYSTEM_WINDOWS_GUI 2
#define IMAGE_SUBSYSTEM_WINDOWS_CONSOLE 3
#define IMAGE_SUBSYSTEM_EFI 10
#define IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER 11
#define IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER 12
#define IMAGE_SUBSYSTEM_EFI_ROM 13
#define IMAGE_SUBSYSTEM_WINDOWS_BOOT 15

#define IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE 0x40
#define IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY 0x80
#define IMAGE_DLL_CHARACTERISTICS_NX_COMPACT 0x100
#define IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION 0x200
#define IMAGE_DLL_CHARACTERISTICS_NO_SEH 0x400
#define IMAGE_DLL_CHARACTERISTICS_NO_BIND 0x800
#define IMAGE_DLL_CHARACTERISTICS_APP_CONTAINER 0x1000
#define IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER 0x2000
#define IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE 0x8000

#define IMAGE_DIRECTORY_ENTRY_EXPORT 0
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE 2
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION 3
#define IMAGE_DIRECTORY_ENTRY_SECURITY 4
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5
#define IMAGE_DIRECTORY_ENTRY_DEBUG 6
#define IMAGE_DIRECTORY_ENTRY_ARCHITECTURE 7
#define IMAGE_DIRECTORY_ENTRY_GLOBALPTR 8
#define IMAGE_DIRECTORY_ENTRY_TLS 9
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG 10
#define IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT 11
#define IMAGE_DIRECTORY_ENTRY_IAT 12
#define IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT 13
#define IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR 14

typedef struct _OPTIONAL_HEADER_TABLE64 
{
    UINT16 Signature;
    UINT8 MajorLinkerVersion;
    UINT8 MinorLinkerVersion;
    UINT32 CodeSize;
    UINT32 InitializedDataSize;
    UINT32 UninitializedDataSize;
    RVA EntrypointRVA;
    UINT32 CodeBase;
    UINT64 ImageBase;
    UINT32 SectionAlignment;
    UINT32 FileAlignment;
    UINT16 MajorOSVersion;
    UINT16 MinorOSVersion;
    UINT16 MajorImageVersion;
    UINT16 MinorImageVersion;
    UINT16 MajorSubsystemVersion;
    UINT16 MinorSubsystemVersion;
    UINT32 Win32Version;
    union
    {
        UINT32 ImageSize;
        UINT32 SizeOfImage;
    };
    union
    {
        UINT32 SizeOfHeaders;
        UINT32 HeadersSize;
    };
    UINT32 Checksum;
    UINT16 Subsystem;
    UINT16 DLLCharacteristics;
    UINT64 StackReserveSize;
    UINT64 StackCommitSize;
    UINT64 HeapReserveSize;
    UINT64 HeapCommitSize;
    UINT32 LoaderFlags;
    UINT32 NumberOfDataDirectories;
    IMAGE_DATA_DIRECTORY DataDirectories[0];
}IMAGE_OPTIONAL_HEADER64, *PIMAGE_OPTIONAL_HEADER64;

typedef struct _IMAGE_EXPORT_DIRECTORY_ENTRY
{
    UINT32 Characteristics;
    UINT32 TimeDateStamp;
    UINT16 MajorVersion;
    UINT16 MinorVersion;
    RVA NameRVA;
    UINT32 OrdinalBase;
    UINT32 NumberOfFunctions;
    UINT32 NumberOfNames;
    RVA AddressOfFunctionsRVA;
    RVA AddressOfNamesRVA;
    RVA AddressOfNameOrdinalsRVA;
}IMAGE_EXPORT_DIRECTORY_ENTRY, *PIMAGE_EXPORT_DIRECTORY_ENTRY;

typedef struct _IMAGE_EXPORT_ADDRESS_TABLE
{
    RVA AddressRVA;
    RVA NameRVA;
}IMAGE_EXPORT_ADDRESS_ENTRY;

typedef struct _IMPORT_DESCRIPTOR 
{
    RVA OriginalFirstThunk;
    UINT32 TimeDateStamp;
    UINT32 ForwardedStamp;
    RVA NameRVA;
    RVA FirstThunkRVA;
}IMAGE_IMPORT_DESCRIPTOR, *PIMAGE_IMPORT_DESCRIPTOR;

typedef struct _IMPORT_TABLE 
{
    IMAGE_IMPORT_DESCRIPTOR Entries[0];
}*PIMAGE_IMPORT_DIRECTORY_ENTRY, IMAGE_IMPORT_DIRECTORY_ENTRY;

#define IMAGE_FILE_RELOC_STRIPPED 0x1
#define IMAGE_FILE_EXECUTABLE 0x2
#define IMAGE_FILE_LINES_STRIPPED 0x4
#define IMAGE_FILE_SYMBOLS_STRIPPED 0x8
#define IMAGE_FILE_LARGE_ADDRESS_AWARE 0x20
#define IMAGE_FILE_32BIT_MACHINE 0x100
#define IMAGE_FILE_DEBUG_STRIPPED 0x200
#define IMAGE_FILE_SYSTEM 0x1000
#define IMAGE_FILE_DLL 0x2000

typedef struct _PE_FILE_TABLE
{
    UINT32 Signature;
    IMAGE_COFF_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
}IMAGE_PE_HEADER, *PIMAGE_PE_HEADER;

typedef struct _IMAGE_SECTION_HEADER
{
    UINT8 Name[8];
    UINT32 VirtualSize;
    RVA VirtualAddressRVA;
    UINT32 SizeOfSection;
    RVA PointerToDataRVA;
    RVA RelocationsPointerRVA;
    RVA LineNumbersPointerRVA;
    UINT16 NumberOfRelocations;
    UINT16 NumberOfLineNumbers;
    UINT32 Characteristics;
}IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;

typedef struct _IMAGE_IMPORT_BY_NAME 
{
    UINT16 Hint;
    CHAR8 Name[0];
}IMPORT_BY_NAME, IMAGE_IMPORT_BY_NAME;

typedef struct _IMAGE_ILT_ENTRY64
{
    union
    {
        struct
        {
            UINT64 NameRVA : 31;
            UINT64 : 32;
            UINT64 Mode : 1;
        };
        struct
        {
            UINT64 Ordinal : 16;
            UINT64 : 37;
            UINT64 : 1;
        };
        UINT64 AsNumber;
    };
}IMAGE_ILT_ENTRY64;

#define IMAGE_SCN_CNT_CODE                    0x00000020UL
#define IMAGE_SCN_CNT_INITIALIZED_DATA        0x00000040UL
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA     0x00000080UL
#define IMAGE_SCN_MEM_EXECUTE                0x20000000UL
#define IMAGE_SCN_MEM_READ                    0x40000000UL
#define IMAGE_SCN_MEM_WRITE                    0x80000000UL


#pragma pack(pop)
#endif
