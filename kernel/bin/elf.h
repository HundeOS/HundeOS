#ifndef __KERNEL_BIN_ELF_H_
#define __KERNEL_BIN_ELF_H_

#include <stdint.h>
#include <stdint.h>

#include "../address.h"
#include "../types/fixed_array.h"

typedef uint8_t byte;

typedef byte TargetOS;
typedef uint64_t Offset;
typedef uint64_t Alignment;
typedef uint16_t Size;
typedef uint64_t ProgramHeaderSize;
typedef uint16_t EntrySize;
typedef uint32_t ElfVersion;
typedef uint16_t ElfType;
typedef uint16_t ElfMachine;
typedef uint64_t SectionFlags;
typedef uint32_t ProgramHeaderFlags;
typedef uint64_t Alignment;
typedef uint32_t ElfFlags;
typedef uint16_t HeaderEntries;
typedef uint32_t SectionType;
typedef uint32_t SectionIndex;
typedef uint32_t ProgramHeaderType;
typedef uint16_t NameIndex;
typedef uint32_t SectionInfo;
typedef uint32_t NameOffset;

typedef enum ElfTypeVals {
    Unknown=0x00,
    Relocatable=0x01,
    Executable=0x02,
    Shared=0x03,
    Core=0x04,
    ReservedOSLow=0xFE00,
    ReservedOSHigh=0xFEFF,
    ReservedProcessorLow=0xFF00,
    ReservedProcessorHigh=0xFFFF,
} ElfTypeVals;

typedef enum ElfMachineVals {
    Aarch64=0xB7
} ElfMachineVals;

typedef enum ElfSectionTypeVals {
    Null=0x00,
    ProgramData=0x01,
    SymbolTable=0x02,
    StringTable=0x03,
    RelocationEntriesAddends=0x04,
    SymbolHashTable=0x05,
    DynamicLinkInfo=0x06,
    Note=0x07,
    NoDataProgramSpace=0x08,
    RelocationEntries=0x09,
    Shlib=0x0A,
    DynamicSymbolTable=0x0B,
    ConstructorArray=0x0E,
    DestructorArray=0x0F,
    PreInitArray=0x10,
    Group=0x11,
    ExtendedTableIndexes=0x12,
    Num=0x13,
    LoOS=0x60000000,
} ElfSectionTypeVals;

typedef enum ProgramHeaderFlagsVals
{
    Execute=0x1,
    Write=0x2,
    Read=0x4,
} ProgramHeaderFlagsVals;

typedef enum ElfProgramHeaderTypeVals
{
    TypeNull=0x00000000,
    Load=0x00000001,
    Dynamic=0x00000002,
    Interpreter=0x00000003,
    AuxiliaryInfo=0x00000004,
    ShLib=0x00000005,
    ProgramHeaderSegment=0x00000006,
    ThreadlocalTemplate=0x00000007,
    OSReservedLow=0x60000000,
    OSReservedHigh=0x6FFFFFFF,
    ProcessorReservedLow=0x70000000,
    ProcessorReservedHigh=0x7FFFFFFF,
} ProgramHeaderTypeVals;

typedef enum SectionFlagsVals {
    Writable=0x1,
    OccupiesMemoryDuringExecution=0x2,
    SectionExecutable=0x4,
    MightBeMerged=0x10,
    NullTerminatedStrings=0x20,
    SHTIndex=0x40,
    PreserveOrder=0x80,
    OsNonConforming=0x100,
    MemberOfGroup=0x200,
    ThreadLocalData=0x400,
    OSSpecificMask=0x0FF00000,
    ProcessorSpecific=0xF0000000,
    SolarisSpecialOrdering=0x4000000,
    SolarisSectionExcluded=0x8000000,
} SectionFlagsVals;

typedef enum Endianess
{
    LittleEndian = 1,
    BigEndian = 2,
} Endianess;

typedef struct Elf64Header
{
    byte magic[4];
    byte klass;
    byte endianness;
    byte ei_version;
    TargetOS target_OS;
    byte abi_version;
    byte padding[7];
    ElfType type;
    ElfMachine machine;
    ElfVersion elf_version;
    Address entry;
    Offset program_header_offset;
    Offset section_header_offset;
    ElfFlags flags;
    Size header_size;
    EntrySize program_header_entry_size;
    HeaderEntries program_header_entries;
    Size section_header_size;
    HeaderEntries section_header_entries;
    NameIndex section_name_index;
} Elf64Header;

typedef struct ProgramHeader {
    ProgramHeaderType type;
    ProgramHeaderFlags flags;
    Offset segment_offset;
    Address virtual_address;
    Address physical_address;
    ProgramHeaderSize file_size;
    ProgramHeaderSize memory_size;
    Alignment alignment;
} ProgramHeader;

typedef struct SectionHeader {
    NameOffset name_offset;
    SectionType type;
    SectionFlags flags;
    Address virtual_address;
    Offset offset;
    Size size;
    SectionIndex link;
    SectionInfo info;
    Alignment alignment;
    Size entry_size;
} SectionHeader;

typedef struct {
    Address vaddr;      // target VA
    Offset off;         // file offset
    Size filesz;        // bytes to read from file
    Size memsz;         // total size in memory (≥ filesz)
    ElfFlags flags;     // PF_R/W/X
    Alignment align;    // usually 0x1000
} LoadSeg;

typedef struct Elf {
    const Address start;
    const Address end;
    BString name;
} Elf;

typedef struct ExpandedSegment
{
    UserAddress address;
    size_t size;
} ExpandedSegment;

typedef Elf * ElfPtr;

ResultDef(Elf);
ResultDef(ElfPtr);
OptionalDef(Elf);
OptionalDef(ElfPtr);

OptionalType(SectionHeader *, SectionHeaderPtr);
OptionalType(ProgramHeader *, ProgramHeaderPtr);
OptionalDef(ExpandedSegment);

FixedArrayDefine(SectionHeaderPtr, 64, SectionHeaderArray, &Ptr__equals);
FixedArrayDefine(ProgramHeaderPtr, 64, ProgramHeaderArray, &Ptr__equals);

SectionHeaderArray Elf__read_section_headers(Elf self);
ProgramHeaderArray Elf__read_program_headers(Elf self);
Elf64Header * Elf__read_header(Elf self);

Elf Elf__new(Address start, Address end, BString name);

FixedArrayDefine(ExpandedSegment, 64, ExpandedSegmentArray, &Ptr__equals);

typedef ExpandedSegmentArray * ExpandedSegmentArrayPtr;

ExpandedSegmentArray Elf__map(Elf self);

#endif