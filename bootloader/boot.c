﻿/* TODO: separate out PE32 loading */
/* TODO: finish import resolving */

#include <efi.h>
#include <efilib.h>
#include <nnxtype.h>
#include <nnxcfg.h>
#include <nnxpe.h>
#include <bootdata.h>

#define ALLOC(x) AllocateZeroPool(x)
#define DEALLOC(x) FreePool(x)

#include <klist.h>
#include <HAL/physical_allocator.h>

/* kinda ugly, but gets the job done (and is far prettier than the mess that it was before) */
#define return_if_error_a(status, d) if (EFI_ERROR(status)) { if(d) Print(L"Line: %d Status: %r\n", __LINE__, status); return status; }
#define return_if_error(status) return_if_error_a(status, 0)
#define return_if_error_debug(status) return_if_error_a(status, 1)

const CHAR16 *wszKernelPath = L"efi\\boot\\NNXOSKRN.exe";

EFI_BOOT_SERVICES* gBootServices;

typedef struct _MODULE_EXPORT
{
    char *Name;
    PVOID Address;
}MODULE_EXPORT, *PMODULE_EXPORT;

VOID DestroyLoadedModule(PVOID modulePointer)
{
    LOADED_BOOT_MODULE* module = (LOADED_BOOT_MODULE*) modulePointer;
    FreePool(module);
}

KLINKED_LIST LoadedModules;

BOOL CompareModuleName(PVOID a, PVOID b)
{
    CHAR* name = b;
    CHAR* moduleName = ((LOADED_BOOT_MODULE*) a)->Name;

    return strcmpa(name, moduleName) == 0;
}

EFI_STATUS TryToLoadModule(CHAR* name)
{
    return EFI_UNSUPPORTED;
}

EFI_STATUS HandleImportDirectory(LOADED_BOOT_MODULE* module, IMAGE_IMPORT_DIRECTORY_ENTRY* importDirectoryEntry)
{
    EFI_STATUS status;
    PVOID imageBase = module->ImageBase;

    IMAGE_IMPORT_DESCRIPTOR* current = importDirectoryEntry->Entries;

    while (current->NameRVA != 0)
    {
        CHAR* name = (CHAR*)((ULONG_PTR)current->NameRVA + (ULONG_PTR)imageBase);
        IMAGE_ILT_ENTRY64* imports = (IMAGE_ILT_ENTRY64*)((ULONG_PTR)current->FirstThunkRVA + (ULONG_PTR)imageBase);
        
        PKLINKED_LIST moduleEntry = FindInListCustomCompare(&LoadedModules, name, CompareModuleName);

        Print(L"%a:\n", name);

        if (moduleEntry == NULL)
        {
            status = TryToLoadModule(name);
            if (status)
                return EFI_LOAD_ERROR;

            moduleEntry = FindInListCustomCompare(&LoadedModules, name, CompareModuleName);

            if (moduleEntry == NULL)
                return EFI_ABORTED;
        }
        
        while (imports->AsNumber)
        {
            if (imports->Mode == 0)
            {
                Print(L"   %a\n", imports->NameRVA + (ULONG_PTR)imageBase + 2);
            }
            else
            {
                Print(L"   #%d\n", imports->Ordinal);
            }
            imports++;
        }

        current++;
    }

    return EFI_SUCCESS;
}

EFI_STATUS LoadImage(EFI_FILE_HANDLE file, OPTIONAL PVOID imageBase, PLOADED_BOOT_MODULE* outModule)
{
    EFI_STATUS status;
    IMAGE_DOS_HEADER dosHeader;
    IMAGE_PE_HEADER peHeader;

    UINTN dataDirectoryIndex;
    DATA_DIRECTORY dataDirectories[16];
    UINTN numberOfDataDirectories, sizeOfDataDirectories;

    SECTION_HEADER* sectionHeaders;
    UINTN numberOfSectionHeaders, sizeOfSectionHeaders;
    SECTION_HEADER* currentSection;

    PKLINKED_LIST moduleLinkedListEntry;
    PLOADED_BOOT_MODULE module;

    UINTN dosHeaderSize = sizeof(dosHeader);
    UINTN peHeaderSize = sizeof(peHeader);

    status = file->Read(file, &dosHeaderSize, &dosHeader);
    return_if_error(status);
    
    if (dosHeader.Signature != IMAGE_MZ_MAGIC)
        return EFI_UNSUPPORTED;

    status = file->SetPosition(file, dosHeader.e_lfanew);
    return_if_error(status);

    status = file->Read(file, &peHeaderSize, &peHeader);
    return_if_error(status);

    if (peHeader.Signature != IMAGE_PE_MAGIC)
        return EFI_UNSUPPORTED;

    if (peHeader.OptionalHeader.Signature != IMAGE_OPTIONAL_HEADER_NT64 ||
        peHeader.FileHeader.Machine != IMAGE_MACHINE_X64)
    {
        Print(L"%a: File specified is not a 64 bit executable\n", __FUNCDNAME__);
        return EFI_UNSUPPORTED;
    }

    if (imageBase == NULL)
        imageBase = (PVOID) peHeader.OptionalHeader.ImageBase;

    /* read all data directories */
    numberOfDataDirectories = peHeader.OptionalHeader.NumberOfDataDirectories;

    if (numberOfDataDirectories > 16)
        numberOfDataDirectories = 16;

    sizeOfDataDirectories = numberOfDataDirectories * sizeof(DATA_DIRECTORY);

    status = file->Read(file, &sizeOfDataDirectories, dataDirectories);
    return_if_error(status);

    /* read all sections */
    numberOfSectionHeaders = peHeader.FileHeader.NumberOfSections;
    sizeOfSectionHeaders = numberOfSectionHeaders * sizeof(SECTION_HEADER);
    sectionHeaders = AllocateZeroPool(sizeOfSectionHeaders);
    if (sectionHeaders == NULL)
        return EFI_OUT_OF_RESOURCES;
    status = file->SetPosition(file, dosHeader.e_lfanew + sizeof(peHeader) + peHeader.OptionalHeader.NumberOfDataDirectories * sizeof(DATA_DIRECTORY));
    return_if_error(status);
    status = file->Read(file, &sizeOfSectionHeaders, sectionHeaders);

    for (currentSection = sectionHeaders; currentSection < sectionHeaders + numberOfSectionHeaders; currentSection++)
    {
        UINTN sectionSize;

        status = file->SetPosition(file, currentSection->PointerToDataRVA);
        sectionSize = (UINTN)currentSection->SizeOfSection;
        
        if (!EFI_ERROR(status))
            status = file->Read(file, &sectionSize, (PVOID)((ULONG_PTR) currentSection->VirtualAddressRVA + (ULONG_PTR)imageBase));
        
        if (EFI_ERROR(status))
        {
            FreePool(sectionHeaders);
            return status;
        }
    }

    /* add this module to the loaded module list
     * if for any reason it is not possible to finish loading, remember to remove from the list */
    moduleLinkedListEntry = AppendList(&LoadedModules, AllocateZeroPool(sizeof(LOADED_BOOT_MODULE)));
    if (moduleLinkedListEntry == NULL)
        return EFI_OUT_OF_RESOURCES;

    module = ((LOADED_BOOT_MODULE*) moduleLinkedListEntry->Value);
    module->ImageBase = imageBase;
    module->Entrypoint = (PVOID)(peHeader.OptionalHeader.EntrypointRVA + (ULONG_PTR)imageBase);
    module->ImageSize = peHeader.OptionalHeader.SizeOfImage;
    module->Name = "";
    module->SectionHeaders = sectionHeaders;
    module->NumberOfSectionHeaders = numberOfSectionHeaders;

    for (dataDirectoryIndex = 0; dataDirectoryIndex < numberOfDataDirectories; dataDirectoryIndex++)
    {
        PVOID dataDirectory = (PVOID)((ULONG_PTR) dataDirectories[dataDirectoryIndex].VirtualAddressRVA + (ULONG_PTR) imageBase);

        if (dataDirectories[dataDirectoryIndex].Size == 0 || dataDirectories[dataDirectoryIndex].VirtualAddressRVA == 0)
            continue;

        if (dataDirectoryIndex == IMAGE_DIRECTORY_ENTRY_EXPORT)
        {
            IMAGE_EXPORT_DIRECTORY_ENTRY* exportDirectoryEntry = (IMAGE_EXPORT_DIRECTORY_ENTRY*) dataDirectory;

            return_if_error(status);
        }
        else if (dataDirectoryIndex == IMAGE_DIRECTORY_ENTRY_IMPORT)
        {
            IMAGE_IMPORT_DIRECTORY_ENTRY* importDirectoryEntry = (IMAGE_IMPORT_DIRECTORY_ENTRY*) dataDirectory;

            status = HandleImportDirectory(module, importDirectoryEntry);
            return_if_error(status);
        }
    }

    *outModule = module;

    return status;
}

EFI_STATUS QueryGraphicsInformation(BOOTDATA* bootdata)
{
    EFI_STATUS status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* graphicsProtocol;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mode;

    status = gBootServices->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, &graphicsProtocol);
    return_if_error(status);

    mode = graphicsProtocol->Mode->Info;

    bootdata->dwHeight = mode->VerticalResolution;
    bootdata->dwWidth = mode->HorizontalResolution;
    bootdata->dwPixelsPerScanline = mode->PixelsPerScanLine;
    bootdata->pdwFramebuffer = (PDWORD)graphicsProtocol->Mode->FrameBufferBase;
    bootdata->pdwFramebufferEnd = (PDWORD)((ULONG_PTR)graphicsProtocol->Mode->FrameBufferBase + (ULONG_PTR)graphicsProtocol->Mode->FrameBufferSize);

    return EFI_SUCCESS;
}

EFI_STATUS QueryMemoryMap(BOOTDATA* bootdata)
{
    EFI_STATUS status;
    UINTN memoryMapSize = 0, memoryMapKey, descriptorSize;
    EFI_MEMORY_DESCRIPTOR* memoryMap = NULL;
    UINT32 descriptorVersion;
    EFI_MEMORY_DESCRIPTOR* currentDescriptor;
    UINTN pages = 0;

    PMMPFN_ENTRY pageFrameEntries;

    do
    {
        if (memoryMap != NULL)
            FreePool(memoryMap);

        status = gBootServices->AllocatePool(EfiLoaderData, memoryMapSize, &memoryMap);
        if (EFI_ERROR(status) || memoryMap == NULL)
            return EFI_ERROR(status) ? status : EFI_OUT_OF_RESOURCES;

        status = gBootServices->GetMemoryMap(&memoryMapSize, memoryMap, &memoryMapKey, &descriptorSize, &descriptorVersion);
        
        memoryMapSize += descriptorSize;
    }
    while (status == EFI_BUFFER_TOO_SMALL);
    return_if_error(status);

    currentDescriptor = memoryMap;
    while (currentDescriptor <= (EFI_MEMORY_DESCRIPTOR*)((ULONG_PTR)memoryMap + memoryMapSize))
    {
        pages += currentDescriptor->NumberOfPages;
        currentDescriptor = (EFI_MEMORY_DESCRIPTOR*) ((ULONG_PTR)currentDescriptor + descriptorSize);
    }

    pageFrameEntries = AllocateZeroPool(pages * sizeof(MMPFN_ENTRY));

    for (int i = 0; i < pages; i++)
        pageFrameEntries[i].Flags = 1;

    currentDescriptor = memoryMap;
    while (currentDescriptor <= (EFI_MEMORY_DESCRIPTOR*) ((ULONG_PTR) memoryMap + memoryMapSize))
    {
        UINTN relativePageIndex;
        ULONG_PTR flags = 1;
        
        switch (currentDescriptor->Type)
        {
            case EfiConventionalMemory:
                flags = 0;
                break;
            default:
                flags = 1;
                break;
        }

        for (relativePageIndex = 0; relativePageIndex < currentDescriptor->NumberOfPages; relativePageIndex++)
        {
            UINTN pageIndex = currentDescriptor->PhysicalStart / PAGE_SIZE + relativePageIndex;

            pageFrameEntries[pageIndex].Flags = flags;
            
            if ((ULONG_PTR)pageIndex * PAGE_SIZE >= (ULONG_PTR)pageFrameEntries &&
                (ULONG_PTR)pageIndex * PAGE_SIZE <= ((ULONG_PTR) pageFrameEntries + pages))
            {
                pageFrameEntries[pageIndex].Flags = 1;
                continue;
            }
        }

        currentDescriptor = (EFI_MEMORY_DESCRIPTOR*) ((ULONG_PTR) currentDescriptor + descriptorSize);
    }

    pageFrameEntries[0].Flags = 1;

    do
    {
        if (memoryMap != NULL)
            FreePool(memoryMap);

        status = gBootServices->AllocatePool(EfiLoaderData, memoryMapSize, &memoryMap);
        if (EFI_ERROR(status) || memoryMap == NULL)
            return EFI_ERROR(status) ? status : EFI_OUT_OF_RESOURCES;

        status = gBootServices->GetMemoryMap(&memoryMapSize, memoryMap, &memoryMapKey, &descriptorSize, &descriptorVersion);

        memoryMapSize += descriptorSize;
    }
    while (status == EFI_BUFFER_TOO_SMALL);
    return_if_error(status);

    bootdata->mapKey = memoryMapKey;
    bootdata->NumberOfPageFrames = pages;
    bootdata->PageFrameDescriptorEntries = pageFrameEntries;

    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* systemTable)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem;
    EFI_FILE_HANDLE root, kernelFile;
    BOOTDATA bootdata;
    VOID (*kernelEntrypoint)(BOOTDATA*);
    PLOADED_BOOT_MODULE module;

    gBootServices = systemTable->BootServices;

    InitializeLib(imageHandle, systemTable);

    status = gBootServices->HandleProtocol(imageHandle, &gEfiLoadedImageProtocolGuid, &loadedImage);
    return_if_error_debug(status);

    status = gBootServices->HandleProtocol(loadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, &filesystem);
    return_if_error_debug(status);

    status = filesystem->OpenVolume(filesystem, &root);
    return_if_error_debug(status);

    status = root->Open(root, &kernelFile, (CHAR16*)wszKernelPath, EFI_FILE_MODE_READ, 0);
    return_if_error_debug(status);

    status = LoadImage(kernelFile, (PVOID)KERNEL_INITIAL_ADDRESS, &module);
    return_if_error_debug(status);

    kernelEntrypoint = module->Entrypoint;
    Print(L"Kernel entrypoint %X\n", kernelEntrypoint);

    bootdata.KernelBase = module->ImageBase;
    bootdata.dwKernelSize = module->ImageSize;
    bootdata.ExitBootServices = gBootServices->ExitBootServices;
    bootdata.pImageHandle = imageHandle;
    bootdata.MainKernelModule = *module;

    LibGetSystemConfigurationTable(&AcpiTableGuid, &bootdata.pRdsp);

    status = QueryGraphicsInformation(&bootdata);
    return_if_error_debug(status);

    status = QueryMemoryMap(&bootdata);
    return_if_error_debug(status);

    Print(L"Launching kernel\n");
    kernelEntrypoint(&bootdata);

    ClearListAndDestroyValues(&LoadedModules, DestroyLoadedModule);

    kernelFile->Close(kernelFile);
    root->Close(root);

    Print(L"Returning to EFI\n");
    return EFI_LOAD_ERROR;
}
