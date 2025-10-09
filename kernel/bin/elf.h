#ifndef __KERNEL_BIN_ELF_H_
#define __KERNEL_BIN_ELF_H_

#include <stddef.h>
#include <stdint.h>
#include <stdint.h>
#include <stdint.h>
#include <stdint.h>
#include <stdint.h>
#include <stdint.h>
#include <stdint.h>
#include <stdint.h>

#include "../kernel.h"
typedef uint8_t byte;

typedef byte TargetOS;
typedef uint64_t Offset;
typedef uint64_t Alignment;
typedef uint16_t Size;
typedef uint16_t EntrySize;
typedef uint32_t ElfVersion;
typedef uint16_t ElfType;
typedef uint16_t ElfMachine;
typedef uint64_t SectionFlags;
typedef uint32_t ElfFlags;
typedef uint16_t HeaderEntries;
typedef uint32_t SectionType;
typedef uint32_t SectionIndex;
typedef uint16_t NameIndex;

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
    SectionType type;
    SectionFlags flags;
    Offset segment_offset;
    Address virtual_address;
    Size file_size;
    Size memory_size;
    uint64_t alignment;
} ProgramHeader;


typedef struct SectionHeader {
    uint32_t name_offset;
    SectionType type;
    SectionFlags flags;
    Address virtual_address;
    Offset offset;
    Size size;
    SectionIndex link;
    uint32_t info;
    uint64_t alignment;
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

int elf_load_binary(const char *path, LoadSeg *segs, int nsegs);

#endif