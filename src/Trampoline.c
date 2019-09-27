/***************************************************************************************************

  Zyan Hook Library (Zyrex)

  Original Author : Florian Bernd

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.

***************************************************************************************************/

#include <Windows.h>
#include <Zycore/LibC.h>
#include <Zycore/Vector.h>
#include <Zydis/Zydis.h>
#include <Zyrex/Internal/Trampoline.h>
#include <Zyrex/Internal/Utils.h>

/* ============================================================================================== */
/* Constants                                                                                      */
/* ============================================================================================== */

/**
 * @brief   Defines the maximum amount of instruction bytes that can be saved to a trampoline.
 *
 * This formula is based on the following edge case consideration:
 * - If `SIZEOF_SAVED_INSTRUCTIONS == SIZEOF_RELATIVE_JUMP - 1 == 4`
 *   - We have to save exactly one additional instruction
 *   - We already saved 4 bytes
 *   - The additional instructions maximum length is 15 bytes
 */
#define ZYREX_TRAMPOLINE_MAX_CODE_SIZE \
    (ZYDIS_MAX_INSTRUCTION_LENGTH + ZYREX_SIZEOF_RELATIVE_JUMP - 1)

/**
 * @brief   Defines the maximum amount of instruction bytes that can be saved to a trampoline
 *          including the backjump.
 */
#define ZYREX_TRAMPOLINE_MAX_CODE_SIZE_WITH_BACKJUMP \
    (ZYREX_TRAMPOLINE_MAX_CODE_SIZE + ZYREX_SIZEOF_ABSOLUTE_JUMP)

/**
 * @brief   Defines the maximum amount of instructions that can be saved to a trampoline.
 */
#define ZYREX_TRAMPOLINE_MAX_INSTRUCTION_COUNT \
    (ZYREX_SIZEOF_RELATIVE_JUMP)

/**
 * @brief   Defines the trampoline region signature.
 */
#define ZYREX_TRAMPOLINE_REGION_SIGNATURE   'zrex'

/* ============================================================================================== */
/* Enums and types                                                                                */
/* ============================================================================================== */

/* ---------------------------------------------------------------------------------------------- */
/* Translation map                                                                                */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Defines the `ZyrexInstructionTranslationItem` struct.
 */
typedef struct ZyrexInstructionTranslationItem_
{
    /**
     * @brief   The offset of a single instruction relative to the beginning of the original
     *          code.
     */
    ZyanU8 offset_original;
    /**
     * @brief   The offset of a single instruction relative to the beginning of the trampoline
     *          code.
     */
    ZyanU8 offset_trampoline;
} ZyrexInstructionTranslationItem;

/**
 * @brief   Defines the `ZyrexInstructionTranslationMap` struct.
 */
typedef struct ZyrexInstructionTranslationMap_
{
    /**
     * @brief   The number of items in the translation map.
     */
    ZyanU8 count;
    /**
     * @brief   The translation items.
     */
    ZyrexInstructionTranslationItem items[ZYREX_TRAMPOLINE_MAX_INSTRUCTION_COUNT];
} ZyrexInstructionTranslationMap;

/* ---------------------------------------------------------------------------------------------- */
/* Trampoline chunk                                                                               */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Defines the `ZyrexTrampolineChunk` struct.
 */
typedef struct ZyrexTrampolineChunk_
{
    /**
     * @brief   Signals, if the trampoline chunk is used.
     */
    ZyanBool is_used;

#if defined(ZYAN_X64)

    /**
     * @brief   The address of the callback function.
     */
    ZyanUPointer callback_address;
    /**
     * @brief   The absolute jump to the callback function.
     */
    ZyanU8 callback_jump[ZYREX_SIZEOF_ABSOLUTE_JUMP];

#endif

    /**
     * @brief   The backjump address.
     */
    ZyanUPointer backjump_address;
    /**
     * @brief   The trampoline code including the backjump to the hooked function.
     */
    ZyanU8 trampoline[ZYREX_TRAMPOLINE_MAX_CODE_SIZE_WITH_BACKJUMP];
    /**
     * @brief   The instruction translation map.
     */
    ZyrexInstructionTranslationMap map;
} ZyrexTrampolineChunk;

/* ---------------------------------------------------------------------------------------------- */
/* Trampoline region                                                                              */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Defines the `ZyrexTrampolineRegion` union.
 *
 * Note that the header shares memory with the first chunk in the trampoline-region.
 */
typedef union ZyrexTrampolineRegion_
{
    /**
     * @brief   The header of the trampoline-region.
     */
    struct
    {
        /**
         * @brief   The signature of the trampoline-region.
         */
        ZyanU32 signature;
        /**
         * @rief    The number of unused trampoline-chunks.
         */
        ZyanUSize number_of_unused_chunks;
    } header;
    /**
     * @brief   The trampoline-chunks.
     */
    ZyrexTrampolineChunk chunks[1];
} ZyrexTrampolineRegion;

ZYAN_STATIC_ASSERT(sizeof(ZyrexTrampolineRegion) == sizeof(ZyrexTrampolineChunk));

/* ---------------------------------------------------------------------------------------------- */

/* ============================================================================================== */
/* Globals                                                                                        */
/* ============================================================================================== */

/**
 * @brief   Contains global trampoline API data.
 *
 * Thread-safety is implicitly guaranteed by the transactional API as only one transaction can be
 * started at a time.
 */
static struct
{
    /**
     * @brief   Signals, if the trampoline API is initialized.
     */
    ZyanBool is_initialized;
    /**
     * @brief   The size of a trampoline-region.
     *
     * This value is platform specific and defaults to the allocation-granularity on Windows and
     * the page-size on most other platforms.
     */
    ZyanUSize region_size;
    /**
     * @brief   The maximum amount of chunks per trampoline-region.
     */
    ZyanUSize chunks_per_region;
    /**
     * @brief   Contains a list of all allocated trampoline-regions.
     */
    ZyanVector regions;
} g_trampoline_data =
{
    ZYAN_FALSE, 0, 0, ZYAN_VECTOR_INITIALIZER
};

/* ============================================================================================== */
/* Internal functions                                                                             */
/* ============================================================================================== */

/* ---------------------------------------------------------------------------------------------- */
/* Helper functions                                                                               */
/* ---------------------------------------------------------------------------------------------- */

#ifdef ZYAN_WINDOWS

/**
 * @brief   Returns the amount of bytes that can be read from the memory region starting at the
 *          given `address` up to a maximum size of `size`.
 *
 * @param   address The memory address.
 * @param   size    Receives the amount of bytes that can be read from the memory region which
 *                  contains `address` and defines the upper limit.
 *
 * @return  A zyan status code.
 *
 * This function is used to avoid invalid memory access. Note that this can not be guaranteed in
 * a preemptive multi-threading environment.
 */
static ZyanStatus ZyrexGetSizeOfReadableMemoryRegion(const void* address, ZyanUSize* size)
{
    ZYAN_ASSERT(address);
    ZYAN_ASSERT(size);

    static const DWORD read_mask =
        PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE |
        PAGE_EXECUTE_WRITECOPY |
        PAGE_READONLY |
        PAGE_READWRITE |
        PAGE_WRITECOPY;

    MEMORY_BASIC_INFORMATION info;
    ZyanU8* current_address = (ZyanU8*)address;
    ZyanUSize current_size = 0;
    while (current_size < *size)
    {
        ZYAN_MEMSET(&info, 0, sizeof(info));
        if (!VirtualQuery(current_address, &info, sizeof(info)))
        {
            return ZYAN_STATUS_BAD_SYSTEMCALL;
        }
        if ((info.State != MEM_COMMIT) || !(info.Protect & read_mask))
        {
            *size = current_size;
            return ZYAN_STATUS_SUCCESS;
        }
        current_address = (ZyanU8*)info.BaseAddress + info.RegionSize;
        if (current_size == 0)
        {
            current_size = (ZyanUPointer)address - (ZyanUPointer)info.BaseAddress;
            continue;
        }
        current_size += info.RegionSize;
    }

    return ZYAN_STATUS_SUCCESS;
}

#endif

/* ---------------------------------------------------------------------------------------------- */

#ifdef ZYAN_X64

// TODO: Integrate this function in Zydis `ZyrexCalcAbsoluteAddressRaw`

/**
 * @brief   Calculates the absolute target address value for a relative-branch instruction
 *          or an instruction with `EIP/RIP`-relative memory operand.
 *
 * @param   instruction     A pointer to the `ZydisDecodedInstruction` struct.
 * @param   runtime_address The runtime address of the instruction.
 * @param   result_address  A pointer to the memory that receives the absolute address.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyrexCalcAbsoluteAddress(const ZydisDecodedInstruction* instruction,
    ZyanU64 runtime_address, ZyanU64* result_address)
{
    ZYAN_ASSERT(instruction);
    ZYAN_ASSERT(result_address);
    ZYAN_ASSERT(instruction->attributes & ZYDIS_ATTRIB_IS_RELATIVE);

    // Instruction with EIP/RIP-relative memory operand
    if ((instruction->attributes & ZYDIS_ATTRIB_HAS_MODRM) &&
        (instruction->raw.modrm.mod == 0) &&
        (instruction->raw.modrm.rm  == 5))
    {
        if (instruction->address_width == ZYDIS_ADDRESS_WIDTH_32)
        {
            *result_address = ((ZyanU32)runtime_address + instruction->length +
                (ZyanU32)instruction->raw.disp.value);

            return ZYAN_STATUS_SUCCESS;
        }
        if (instruction->address_width == ZYDIS_ADDRESS_WIDTH_64)
        {
            *result_address = (ZyanU64)(runtime_address + instruction->length +
                instruction->raw.disp.value);

            return ZYAN_STATUS_SUCCESS;
        }
    }

    // Relative branch instruction
    if (instruction->raw.imm[0].is_signed &&
        instruction->raw.imm[0].is_relative)
    {
        *result_address = (ZyanU64)((ZyanI64)runtime_address + instruction->length +
            instruction->raw.imm[0].value.s);
        switch (instruction->machine_mode)
        {
        case ZYDIS_MACHINE_MODE_LONG_COMPAT_16:
        case ZYDIS_MACHINE_MODE_LEGACY_16:
        case ZYDIS_MACHINE_MODE_REAL_16:
        case ZYDIS_MACHINE_MODE_LONG_COMPAT_32:
        case ZYDIS_MACHINE_MODE_LEGACY_32:
            if (instruction->operand_width == 16)
            {
                *result_address &= 0xFFFF;
            }
            break;
        case ZYDIS_MACHINE_MODE_LONG_64:
            break;
        default:
            ZYAN_UNREACHABLE;
        }

        return ZYAN_STATUS_SUCCESS;
    }

    ZYAN_UNREACHABLE;
}

/**
 * @brief   Decodes the assembly code in the given `buffer` and returns the lowest and highest
 *          absolute target addresses of all relative branch instructions and `EIP/RIP`-relative
 *          memory operands.
 *
 * @param   buffer              The buffer to decode.
 * @param   size                The size of the buffer.
 * @param   min_bytes_to_decode The minimum amount of bytes to decode.
 * @param   address_lo          Receives the lowest absolute target address.
 * @param   address_hi          Receives the highest absolute target address.
 *
 * @return  `ZYAN_STATUS_TRUE` if at least one instruction with an relative address was found,
 *          `ZYAN_STATUS_FALSE` if not, or a generic zyan status code if an error occured.
 */
static ZyanStatus ZyrexGetAddressRangeOfRelativeInstructions(const void* buffer, ZyanUSize size,
    ZyanUSize min_bytes_to_decode, ZyanUPointer* address_lo, ZyanUPointer* address_hi)
{
    ZyanStatus result = ZYAN_STATUS_FALSE;

    ZydisDecoder decoder;
#if defined(ZYAN_X86)
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32);
#elif defined(ZYAN_X64)
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);
#else
#   error "Unsupported architecture detected"
#endif

    ZyanUPointer lo = 0;
    ZyanUPointer hi = 0;
    ZydisDecodedInstruction instruction;
    ZyanUSize offset = 0;
    while (offset < min_bytes_to_decode)
    {
        const ZyanStatus status =
            ZydisDecoderDecodeBuffer(&decoder, (ZyanU8*)buffer + offset, size - offset,
                &instruction);

        ZYAN_CHECK(status);

        if (!(instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE))
        {
            offset += instruction.length;
            continue;
        }
        result = ZYAN_STATUS_TRUE;

        ZyanU64 result_address;
        ZYAN_CHECK(ZyrexCalcAbsoluteAddress(&instruction, (ZyanU64)buffer + offset,
            &result_address));

        if (result_address < lo)
        {
            lo = result_address;
        }
        if (result_address > hi)
        {
            hi = result_address;
        }

        offset += instruction.length;
    }

    if (result == ZYAN_STATUS_TRUE)
    {
        *address_lo = lo;
        *address_hi = hi;
    }

    return result;
}

#endif

/* ---------------------------------------------------------------------------------------------- */
/* Trampoline region                                                                              */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Checks, if the given region is in a +/-2GiB range to both passed address values.
 *
 * @param   region_address      The base address of the trampoline region to check.
 * @param   address_lo          The memory address lower bound to be used as condition.
 * @param   address_hi          The memory address upper bound to be used as condition.
 *
 * @return  `ZYAN_STATUS_TRUE` if both address values are in range,
 *          `ZYAN_STATUS_FALSE` if only one address value is in range, or
 *          `ZYAN_STATUS_OUT_OF_RANGE`, if both address values are out of range.
 */
static ZyanBool ZyrexTrampolineRegionInRange(ZyanUPointer region_address,
    ZyanUPointer address_lo, ZyanUPointer address_hi)
{
    ZYAN_ASSERT(g_trampoline_data.is_initialized);
    ZYAN_ASSERT(ZYAN_IS_ALIGNED_TO(region_address, g_trampoline_data.region_size));

    // Skip the first chunk as it shares memory with the region-header
    const ZyanIPointer region_base = region_address + sizeof(ZyrexTrampolineChunk);

    const ZyanIPointer distance_lo =
        region_base - (ZyanIPointer)address_lo + (ZyanIPointer)address_lo < region_base
            ? sizeof(ZyrexTrampolineChunk)
            : sizeof(ZyrexTrampolineChunk) * (g_trampoline_data.chunks_per_region - 1);
    if ((ZYAN_ABS(distance_lo) > ZYREX_RANGEOF_RELATIVE_JUMP))
    {
        return ZYAN_FALSE;
    }

    const ZyanIPointer distance_hi =
        region_base - (ZyanIPointer)address_hi + (ZyanIPointer)address_hi < region_base
            ? sizeof(ZyrexTrampolineChunk)
            : sizeof(ZyrexTrampolineChunk) * (g_trampoline_data.chunks_per_region - 1);
    if ((ZYAN_ABS(distance_hi) > ZYREX_RANGEOF_RELATIVE_JUMP))
    {
        return ZYAN_FALSE;
    }

    return ZYAN_TRUE;
}

/**
 * @brief   Searches the given trampoline-region for an unused `ZyrexTrampolineChunk` item that
 *          lies in a +/-2GiB range to both given addresses.
 *
 * @param   region      A pointer to the `ZyrexTrampolineRegion` struct.
 * @param   address_lo  The memory address lower bound to be used as search condition.
 * @param   address_hi  The memory address upper bound to be used as search condition.
 * @param   chunk       Receives a pointer to a matching `ZyrexTrampolineChunk` struct.
 */
static ZyanBool ZyrexTrampolineRegionFindChunkInRegion(ZyrexTrampolineRegion* region,
    ZyanUPointer address_lo, ZyanUPointer address_hi, ZyrexTrampolineChunk** chunk)
{
    ZYAN_ASSERT(region);
    ZYAN_ASSERT(chunk);
    ZYAN_ASSERT(g_trampoline_data.is_initialized);

    if (region->header.number_of_unused_chunks == 0)
    {
        return ZYAN_FALSE;
    }

    if (!ZyrexTrampolineRegionInRange((ZyanUPointer)region, address_lo, address_hi))
    {
        return ZYAN_FALSE;
    }

    // Skip the first chunk as it shares memory with the region-header
    for (ZyanUSize i = 1; i < g_trampoline_data.chunks_per_region; ++i)
    {
        const ZyanIPointer chunk_base = (ZyanIPointer)&region->chunks[i];

        const ZyanIPointer distance_lo_chunk = chunk_base - (ZyanIPointer)address_lo +
            (((ZyanIPointer)address_lo < chunk_base) ? sizeof(ZyrexTrampolineChunk) : 0);
        if ((ZYAN_ABS(distance_lo_chunk) > ZYREX_RANGEOF_RELATIVE_JUMP))
        {
            continue;
        }

        const ZyanIPointer distance_hi_chunk = chunk_base - (ZyanIPointer)address_hi +
            (((ZyanIPointer)address_hi < chunk_base) ? sizeof(ZyrexTrampolineChunk) : 0);
        if ((ZYAN_ABS(distance_hi_chunk) > ZYREX_RANGEOF_RELATIVE_JUMP))
        {
            continue;
        }

        *chunk = &region->chunks[i];
        return ZYAN_TRUE;
    }

    return ZYAN_FALSE;
}

/**
 * @brief   Searches the global trampoline-region list for an unused `ZyrexTrampolineChunk` item
 *          that lies in a +/-2GiB range to both given addresses.
 *
 * @param   address_lo  The memory address lower bound to be used as search condition.
 * @param   address_hi  The memory address upper bound to be used as search condition.
 * @param   region      Receives a pointer to a matching `ZyrexTrampolineRegion` struct.
 * @param   chunk       Receives a pointer to a matching `ZyrexTrampolineChunk` struct.
 *
 * @return  `ZYAN_STATUS_TRUE` if a valid chunk was found in an already allocated trampoline region,
 *          `ZYAN_STATUS_FALSE` if not, or a generic zyan status code if an error occured.
 */
static ZyanStatus ZyrexTrampolineRegionFindChunk(ZyanUPointer address_lo, ZyanUPointer address_hi,
    ZyrexTrampolineRegion** region, ZyrexTrampolineChunk** chunk)
{
    ZYAN_ASSERT(region);
    ZYAN_ASSERT(chunk);
    ZYAN_ASSERT(g_trampoline_data.is_initialized);

    ZyanUSize size;
    ZYAN_CHECK(ZyanVectorGetSize(&g_trampoline_data.regions, &size));
    if (size == 0)
    {
        goto RegionNotFound;
    }

    const ZyanUSize mid = (address_lo + address_hi) / 2;
    ZyanUSize found_index;
    const ZyanStatus status =
        ZyanVectorBinarySearch(&g_trampoline_data.regions, &mid, &found_index,
            (ZyanComparison)&ZyanComparePointer);
    ZYAN_CHECK(status);

    if (found_index == size)
    {
        --found_index;
    }
    ZyanISize lo = found_index;
    ZyanISize hi = found_index + 1;
    ZyrexTrampolineRegion** element;
    while (ZYAN_TRUE)
    {
        ZyanU8 c = 0;

        if (lo >= 0)
        {
            element = (ZyrexTrampolineRegion**)ZyanVectorGet(&g_trampoline_data.regions, lo--);
            ZYAN_ASSERT(element);
            if (ZyrexTrampolineRegionFindChunkInRegion(*element, address_lo, address_hi, chunk))
            {
                break;
            }
            ++c;
        }
        if (hi < (ZyanISize)size)
        {
            element = (ZyrexTrampolineRegion**)ZyanVectorGet(&g_trampoline_data.regions, hi++);
            ZYAN_ASSERT(element);
            if (ZyrexTrampolineRegionFindChunkInRegion(*element, address_lo, address_hi, chunk))
            {
                break;
            }
            ++c;
        }

        if (c == 0)
        {
            goto RegionNotFound;
        }
    }

    *region = *element;
    return ZYAN_STATUS_TRUE;

RegionNotFound:
    return ZYAN_STATUS_FALSE;
}

/**
 * @brief   Inserts a new `ZyrexTrampolineRegion` item to the global trampoline-region list.
 *
 * @param   region  A pointer to the `ZyrexTrampolineRegion` item.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyrexTrampolineRegionInsert(ZyrexTrampolineRegion* region)
{
    ZYAN_ASSERT(region);
    ZYAN_ASSERT(g_trampoline_data.is_initialized);
    ZYAN_ASSERT(ZYAN_IS_ALIGNED_TO((ZyanUPointer)region, g_trampoline_data.region_size));

    ZyanUSize found_index;
    const ZyanStatus status =
        ZyanVectorBinarySearch(&g_trampoline_data.regions, &region, &found_index,
            (ZyanComparison)&ZyanComparePointer);
    ZYAN_CHECK(status);

    ZYAN_ASSERT(status == ZYAN_STATUS_FALSE);
    return ZyanVectorInsert(&g_trampoline_data.regions, found_index, &region);
}

/**
 * @brief   Removes the given `ZyrexTrampolineRegion` item from the global trampoline-region list.
 *
 * @param   region  A pointer to the `ZyrexTrampolineRegion` item.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyrexTrampolineRegionRemove(ZyrexTrampolineRegion* region)
{
    ZYAN_ASSERT(region);
    ZYAN_ASSERT(g_trampoline_data.is_initialized);
    ZYAN_ASSERT(ZYAN_IS_ALIGNED_TO((ZyanUPointer)region, g_trampoline_data.region_size));

    ZyanUSize found_index;
    const ZyanStatus status =
        ZyanVectorBinarySearch(&g_trampoline_data.regions, &region, &found_index,
            (ZyanComparison)&ZyanComparePointer);
    ZYAN_CHECK(status);

    ZYAN_ASSERT(status == ZYAN_STATUS_TRUE);
    return ZyanVectorDelete(&g_trampoline_data.regions, found_index);
}

/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Changes the memory protection of the passed trampoline-region to `RX`.
 *
 * @param   region  A pointer to the `ZyrexTrampolineRegion` struct.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyrexTrampolineRegionProtect(ZyrexTrampolineRegion* region)
{
    DWORD old_value;
    if (!VirtualProtect(region, sizeof(ZyrexTrampolineChunk), PAGE_EXECUTE_READ, &old_value))
    {
        return ZYAN_STATUS_BAD_SYSTEMCALL;
    }

    return ZYAN_STATUS_SUCCESS;
}

/**
 * @brief   Changes the memory protection of the passed trampoline-region to `RWX`.
 *
 * @param   region  A pointer to the `ZyrexTrampolineRegion` struct.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyrexTrampolineRegionUnprotect(ZyrexTrampolineRegion* region)
{
    DWORD old_value;
    if (!VirtualProtect(region, sizeof(ZyrexTrampolineChunk), PAGE_EXECUTE_READWRITE, &old_value))
    {
        return ZYAN_STATUS_BAD_SYSTEMCALL;
    }

    return ZYAN_STATUS_SUCCESS;
}

/**
 * Allocates memory for a new trampoline region in a +/-2GiB range of both passed address values
 * and initializes it.
 *
 * @param   address_lo  The memory address lower bound.
 * @param   address_hi  The memory address upper bound.
 * @param   region      Receives a pointer to the new `ZyrexTrampolineRegion` struct.
 *
 * Regions allocated by this function will have `RWX` memory protection.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyrexTrampolineRegionAllocate(ZyanUPointer address_lo, ZyanUPointer address_hi,
    ZyrexTrampolineRegion** region)
{
    ZYAN_ASSERT(region);
    ZYAN_ASSERT(g_trampoline_data.is_initialized);

    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);

    const ZyanUSize region_size = g_trampoline_data.region_size;
    const ZyanUPointer mid = (address_lo + address_hi) / 2;
    const ZyanU8* alloc_address_lo =
        (const ZyanU8*)ZYAN_ALIGN_DOWN(mid, (ZyanUPointer)region_size);
    const ZyanU8* alloc_address_hi =
        (const ZyanU8*)ZYAN_ALIGN_UP(mid, (ZyanUPointer)region_size);

    MEMORY_BASIC_INFORMATION memory_info;
    while (ZYAN_TRUE)
    {
        // Skip reserved address regions
        if (alloc_address_lo < (ZyanU8*)system_info.lpMinimumApplicationAddress)
        {
            alloc_address_lo = (const ZyanU8*)system_info.lpMinimumApplicationAddress;
        }
        if (alloc_address_lo > (ZyanU8*)system_info.lpMaximumApplicationAddress)
        {
            alloc_address_lo = (const ZyanU8*)system_info.lpMaximumApplicationAddress;
        }
        if (alloc_address_hi < (ZyanU8*)system_info.lpMinimumApplicationAddress)
        {
            alloc_address_hi = (const ZyanU8*)system_info.lpMinimumApplicationAddress;
        }
        if (alloc_address_hi > (ZyanU8*)system_info.lpMaximumApplicationAddress)
        {
            alloc_address_hi = (const ZyanU8*)system_info.lpMaximumApplicationAddress;
        }

        ZyanU8 c = 0;

        if (ZyrexTrampolineRegionInRange((ZyanUPointer)alloc_address_lo, address_lo, address_hi))
        {
            ZYAN_MEMSET(&memory_info, 0, sizeof(memory_info));
            if (!VirtualQuery(alloc_address_lo, &memory_info, sizeof(memory_info)))
            {
                return ZYAN_STATUS_BAD_SYSTEMCALL;
            }
            if ((memory_info.State == MEM_FREE) && (memory_info.RegionSize >= region_size))
            {
                *region = VirtualAlloc((void*)alloc_address_lo, region_size,
                    MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
                if (*region)
                {
                    goto InitializeRegion;
                }
            }
            alloc_address_lo = (const ZyanU8*)((ZyanUPointer)memory_info.BaseAddress - region_size);
            ++c;
        }

        if (ZyrexTrampolineRegionInRange((ZyanUPointer)alloc_address_hi, address_lo, address_hi))
        {
            memset(&memory_info, 0, sizeof(memory_info));
            if (!VirtualQuery(alloc_address_hi, &memory_info, sizeof(memory_info)))
            {
                return ZYAN_STATUS_BAD_SYSTEMCALL;
            }
            if ((memory_info.State == MEM_FREE) && (memory_info.RegionSize >= region_size))
            {
                *region = VirtualAlloc((void*)alloc_address_hi, region_size,
                    MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
                if (*region)
                {
                    goto InitializeRegion;
                }
            }
            alloc_address_hi = (const ZyanU8*)((ZyanUPointer)memory_info.BaseAddress + region_size);
            ++c;
        }

        if (c == 0)
        {
            return ZYAN_STATUS_OUT_OF_RANGE;
        }
    }

    // ZYAN_UNREACHABLE;

InitializeRegion:
    (*region)->header.signature = ZYREX_TRAMPOLINE_REGION_SIGNATURE;
    (*region)->header.number_of_unused_chunks = g_trampoline_data.chunks_per_region - 1;

    return ZYAN_STATUS_SUCCESS;
}

/**
 * @brief   Frees the memory of the given trampoline region.
 *
 * @param   region  A pointer to the `ZyrexTrampolineRegion` struct.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyrexTrampolineRegionFree(ZyrexTrampolineRegion* region)
{
    ZYAN_ASSERT(region);
    ZYAN_ASSERT((ZyanUPointer)region ==
        ZYAN_ALIGN_DOWN((ZyanUPointer)region, g_trampoline_data.region_size));

    if (!VirtualFree(region, 0, MEM_RELEASE))
    {
        return ZYAN_STATUS_BAD_SYSTEMCALL;
    }

    return ZYAN_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------- */
/* Trampoline chunk                                                                               */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Initializes a new trampoline chunk and relocates the instructions from the original
 *          function.
 *
 * @param   chunk               A pointer to the `ZyrexTrampolineChunk` struct.
 * @param   address             The address of the function to create the trampoline for.
 * @param   callback            The address of the callback function the hook will redirect to.
 * @param   min_bytes_to_reloc  Specifies the minimum amount of  bytes that need to be relocated
 *                              to the trampoline (usually equals the size of the branch
 *                              instruction used for hooking).
 *                              This function might copy more bytes on demand to keep individual
 *                              instructions intact.
 * @param   max_bytes_to_read   The maximum amount of bytes that can be safely read from the given
 *                              `address`.
 * @param   flags               Trampoline creation flags.
 *
 * @return  A zyan status code.
 */
static ZyanStatus ZyrexTrampolineChunkInit(ZyrexTrampolineChunk* chunk, const void* address,
    const void* callback, ZyanUSize min_bytes_to_reloc, ZyanUSize max_bytes_to_read,
    ZyrexTrampolineFlags flags)
{
    ZYAN_ASSERT(chunk);
    ZYAN_ASSERT(address);
    ZYAN_ASSERT(callback);
    ZYAN_ASSERT(min_bytes_to_reloc <= max_bytes_to_read);

    ZYAN_UNUSED(flags);

    chunk->is_used = ZYAN_TRUE;

#if defined(ZYAN_X64)

    chunk->callback_address = (ZyanUPointer)callback;
    ZyrexWriteAbsoluteJump(&chunk->callback_jump, (ZyanUPointer)&chunk->callback_address);

#endif

    // Relocate instructions
    ZydisDecoder decoder;
#if defined(ZYAN_X86)
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_ADDRESS_WIDTH_32);
#elif defined(ZYAN_X64)
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);
#else
#   error "Unsupported architecture detected"
#endif

    ZyanU8 instructions_read = 0;
    ZyanU8 instructions_written = 0;
    ZyanUSize bytes_read = 0;
    ZyanUSize bytes_written = 0;

    ZydisDecodedInstruction instruction;
    while (bytes_read < min_bytes_to_reloc)
    {
        // The code buffer is full
        ZYAN_ASSERT(bytes_written < ZYREX_TRAMPOLINE_MAX_CODE_SIZE);
        // The translation map is full
        ZYAN_ASSERT(instructions_read < ZYREX_TRAMPOLINE_MAX_INSTRUCTION_COUNT);

        const ZyanStatus status =
            ZydisDecoderDecodeBuffer(&decoder, (ZyanU8*)address + bytes_read,
                max_bytes_to_read - bytes_read, &instruction);

        ZYAN_CHECK(status);

        if (instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE)
        {
            switch (instruction.mnemonic)
            {
            case ZYDIS_MNEMONIC_CALL:
            {
                if (!(flags & ZYREX_TRAMPOLINE_FLAG_REWRITE_CALL))
                {
                    return ZYAN_STATUS_FAILED; // TODO:
                }

                if (instruction.attributes & ZYDIS_ATTRIB_HAS_MODRM)
                {
                    // Indirect absolute `CALL` instruction with `EIP/RIP`-relative address

                } else
                {
                    // Relative `CALL` instruction

                }

                // TODO: Rewrite CALL
                break;
            }
            case ZYDIS_MNEMONIC_JCXZ:
            case ZYDIS_MNEMONIC_JECXZ:
            case ZYDIS_MNEMONIC_JRCXZ:
            {
                if (!(flags & ZYREX_TRAMPOLINE_FLAG_REWRITE_JCXZ))
                {
                    return ZYAN_STATUS_FAILED; // TODO:
                }
                // TODO: Rewrite JCXZ
                break;
            }
            case ZYDIS_MNEMONIC_LOOP:
            case ZYDIS_MNEMONIC_LOOPE:
            case ZYDIS_MNEMONIC_LOOPNE:
            {
                if (!(flags & ZYREX_TRAMPOLINE_FLAG_REWRITE_LOOP))
                {
                    return ZYAN_STATUS_FAILED; // TODO:
                }
                // TODO: Rewrite LOOP
                break;
            }
            default:
                break;
            }
        } else
        {
            ZYAN_MEMCPY(&chunk->trampoline[bytes_written],
                (const ZyanU8*)address + bytes_read, instruction.length);
        }

        chunk->map.items[instructions_read].offset_original = (ZyanU8)bytes_read;
        chunk->map.items[instructions_read].offset_trampoline = (ZyanU8)bytes_written;
        bytes_read += instruction.length;
        bytes_written += instruction.length;
        ++instructions_read;
        ++instructions_written;
    }

    ZyrexWriteAbsoluteJump(&chunk->trampoline[bytes_written],
        (ZyanUPointer)&chunk->backjump_address);
    chunk->backjump_address = (ZyanUPointer)address + bytes_read;
    chunk->map.count = instructions_read;

    // Fill remaining bytes with `INT 3` instructions
    const ZyanUSize bytes_remaining = ZYREX_TRAMPOLINE_MAX_CODE_SIZE_WITH_BACKJUMP - bytes_written;
    if (bytes_remaining > 0)
    {
        ZYAN_MEMSET(&chunk->trampoline[bytes_written + ZYREX_SIZEOF_ABSOLUTE_JUMP], 0xCC,
            bytes_remaining);
    }

    return ZYAN_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------------------------------- */

/* ============================================================================================== */
/* Public functions                                                                               */
/* ============================================================================================== */

ZyanStatus ZyrexTrampolineCreate(const void* address, const void* callback,
    ZyanUSize min_bytes_to_reloc, ZyrexTrampoline* trampoline)
{
    return ZyrexTrampolineCreateEx(address, callback, min_bytes_to_reloc,
        ZYREX_TRAMPOLINE_FLAG_REWRITE_CALL |
        ZYREX_TRAMPOLINE_FLAG_REWRITE_JCXZ |
        ZYREX_TRAMPOLINE_FLAG_REWRITE_LOOP, trampoline);
}

ZyanStatus ZyrexTrampolineCreateEx(const void* address, const void* callback,
    ZyanUSize min_bytes_to_reloc, ZyrexTrampolineFlags flags, ZyrexTrampoline* trampoline)
{
    if (!address || !callback || (min_bytes_to_reloc < 1) || !trampoline)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }

    // Check if the memory region of the target function has enough space for the hook code
    ZyanUSize source_size = ZYREX_TRAMPOLINE_MAX_CODE_SIZE;
    ZYAN_CHECK(ZyrexGetSizeOfReadableMemoryRegion(address, &source_size));
    if (source_size < min_bytes_to_reloc)
    {
        return ZYAN_STATUS_INVALID_OPERATION;
    }

    if (!g_trampoline_data.is_initialized)
    {
        ZYAN_CHECK(ZyanVectorInit(&g_trampoline_data.regions, sizeof(ZyrexTrampolineRegion*), 8, 
            ZYAN_NULL));

        SYSTEM_INFO system_info;
        GetSystemInfo(&system_info);
        g_trampoline_data.region_size = system_info.dwAllocationGranularity;
        g_trampoline_data.chunks_per_region =
            g_trampoline_data.region_size / sizeof(ZyrexTrampolineChunk);

        g_trampoline_data.is_initialized = ZYAN_TRUE;
    }

#ifdef ZYAN_X64

    // Gather memory address lower and upper bounds in order to find a suitable memory region for
    // the rampoline
    ZyanUPointer lo = (ZyanUPointer)(-1);
    ZyanUPointer hi = 0;
    ZYAN_CHECK(ZyrexGetAddressRangeOfRelativeInstructions(address, source_size,
        ZYREX_SIZEOF_RELATIVE_JUMP, &lo, &hi));

    const ZyanUPointer address_value = (ZyanUPointer)address;
    if (address_value < lo)
    {
        lo = address_value;
    }
    if (address_value > hi)
    {
        hi = address_value;
    }

    if ((hi - lo) > ZYREX_RANGEOF_RELATIVE_JUMP)
    {
        return ZYAN_STATUS_INVALID_OPERATION;
    }

#endif

    ZyanBool is_new_region = ZYAN_FALSE;
    ZyrexTrampolineRegion* region;
    ZyrexTrampolineChunk* chunk;
    ZyanStatus status = ZyrexTrampolineRegionFindChunk(lo, hi, &region, &chunk);
    ZYAN_CHECK(status);

    switch (status)
    {
    case ZYAN_STATUS_TRUE:
    {
        ZYAN_ASSERT(region);
        ZYAN_ASSERT(chunk);
        ZYAN_CHECK(ZyrexTrampolineRegionUnprotect(region));
        break;
    }
    case ZYAN_STATUS_FALSE:
    {
        ZYAN_CHECK(ZyrexTrampolineRegionAllocate(lo, hi, &region));
        is_new_region = ZyrexTrampolineRegionFindChunkInRegion(region, lo, hi, &chunk);
        ZYAN_ASSERT(is_new_region);
        ZYAN_ASSERT(region);
        ZYAN_ASSERT(chunk);
        break;
    }
    default:
        ZYAN_UNREACHABLE;
    }

    ZYAN_ASSERT(region->header.number_of_unused_chunks > 0);

    status =
        ZyrexTrampolineChunkInit(chunk, address, callback, min_bytes_to_reloc, source_size, flags);
    if (!ZYAN_SUCCESS(status))
    {
        if (is_new_region)
        {
            ZYAN_UNUSED(ZyrexTrampolineRegionFree(region));
        } else
        {
            ZYAN_UNUSED(ZyrexTrampolineRegionProtect(region));
        }
        return status;
    }

    trampoline->address_of_trampoline_code = &chunk->trampoline;

    --region->header.number_of_unused_chunks;
    ZYAN_UNUSED(ZyrexTrampolineRegionProtect(region));

    if (is_new_region)
    {
        ZYAN_UNUSED(ZyrexTrampolineRegionInsert(region));
    }
    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus ZyrexTrampolineFree(const ZyrexTrampoline* trampoline)
{
    if (!trampoline)
    {
        return ZYAN_STATUS_INVALID_ARGUMENT;
    }
    if (!g_trampoline_data.is_initialized)
    {
        return ZYAN_STATUS_INVALID_OPERATION;
    }

    // TODO:

    ZyanUSize size;
    ZYAN_CHECK(ZyanVectorGetSize(&g_trampoline_data.regions, &size));
    if (size == 0)
    {
        ZYAN_CHECK(ZyanVectorDestroy(&g_trampoline_data.regions));
        g_trampoline_data.is_initialized = ZYAN_FALSE;
    }

    return ZYAN_STATUS_SUCCESS;
}

/* ============================================================================================== */
