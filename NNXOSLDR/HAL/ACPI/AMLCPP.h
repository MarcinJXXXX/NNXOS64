#pragma once

#include "AML.h"
#include "nnxllist.h"

AMLObjRef CreateAMLObjRef(VOID* pointer, AMLObjectType type);

class AMLNamedObject;
class AMLScope;
class AMLBuffer;
class AMLPackage;
class AMLMethodDef;
class AMLDevice;
class AMLField;
class AMLOpetationRegion;

class AMLNamedObject {
public:
	AMLName name;
	AMLObjRef object;
	AMLNamedObject(AMLName name, AMLObjRef object);
	static AMLNamedObject* newObject(AMLName name, AMLObjRef object);
};

class AMLBuffer {
public:
	AMLBuffer(UINT32 size);
	UINT8 *data;
private:
	UINT32 size;
};

class AMLPackage {
public:
	AMLPackage(UINT8 numberOfElements);
	UINT8 numElements;					// According to ACPI 6.2 Specification 19.3.5 "ASL Data Types", the size of a package cannot exceed 255
	AMLObjRef* elements;
};

class AMLScope{
public:
	AMLName name;
	AMLScope* parent;
	NNXLinkedList<AMLNamedObject*> namedObjects;
	NNXLinkedList<AMLScope*> children;
	NNXLinkedList<AMLDevice*> devices;
	NNXLinkedList<AMLMethodDef*> methods;
	AMLScope();
	static AMLScope* newScope(const char* name, AMLScope* parent);
	AMLOpetationRegion *opRegion;
};

typedef struct {
	AMLName name;
	AMLScope* scope;
}AMLNameWithinAScope;

class AMLParser
{
public:
	AMLParser(ACPI_DSDT *table);
	void Parse(AMLScope* scope, UINT32 size);
	AMLScope* GetRootScope();
	UINT32 GetSize();
private:
	UINT64 index;
	UINT8* table;
	UINT8 name[4];
	UINT32 size;
	UINT8 revision;
	UINT8 checksum;
	UINT8 oemid[6];
	UINT8 tableid[8];
	UINT32 oemRevision;
	UINT8 compilerName[4];
	UINT32 compilerRevision;

	AMLBuffer* CreateBuffer();
	AMLPackage* CreatePackage();
	AMLMethodDef* CreateMethod(AMLName methodName, AMLScope* scope);
	AMLScope* FindScope(AMLName name, AMLScope* current);

	AMLNameWithinAScope GetNameWithinAScope(AMLScope* current);

	UINT32 DecodePkgLenght();
	UINT8 DecodePackageNumElements();
	VOID GetString(UINT8* output, UINT32 lenght);
	VOID GetString(UINT8* output, UINT32 lenght, UINT8 terminator);
	UINT8 GetByte();
	UINT16 GetWord();
	UINT32 GetDword();
	UINT64 GetQword();
	UINT64 GetInteger();
	UINT32 DecodeBufferSize();
	AMLObjRef GetAMLObject(UINT8 opcode);
	AMLOpetationRegion* CreateOperationRegion(AMLNameWithinAScope);
	AMLName GetName();
	AMLScope root;
};

void PrintName(AMLName name);
inline bool operator==(const AMLName& name1, const AMLName& name2) {
	return (*((DWORD*)name1.name) == *((DWORD*)name2.name));
}

void InitializeNamespace(AMLScope* root);

class AMLDevice : public AMLScope
{
private:
	AMLNamedObject* _HID;
public:
	void Init_HID();
	AMLNamedObject* Get_HID();
	static AMLDevice* newScope(const char* name, AMLScope* parent);
};

UINT64 GetIntegerFromAMLObjRef(AMLObjectReference objRef);

class AMLMethodDef {
public:
	AMLNameWithinAScope name;
	UINT8 parameterNumber;
	AMLParser* parser;
	UINT64 codeIndex;
	UINT64 maxCodeIndex;

	AMLMethodDef(AMLName, AMLParser*, AMLScope*);
};

class AMLField {
public:
	AMLOpetationRegion* parent;
};

class AMLOpetationRegion {
public:
	UINT8 type;
	UINT64 offset;
	UINT64 lenght;
	NNXLinkedList<AMLField*> fields;
	AMLOpetationRegion* previous;
	AMLNameWithinAScope name;
};