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

#ifndef ZYREX_INTERNAL_TRAMPOLINE_H
#define ZYREX_INTERNAL_TRAMPOLINE_H

#include <Zycore/Types.h>
#include <Zyrex/Status.h>
#include <Zyrex/Internal/Utils.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * @brief   Defines an additional amount of bytes to reserve in the trampoline code buffer which
 *          is required in order to rewrite certain kinds of instructions.
 */
#define ZYREX_TRAMPOLINE_MAX_CODE_SIZE_BONUS \
    8

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
 * @brief   Defines an additional amount of slots to reserve in the instruction translation map
 *          which is required in order to rewrite certain kinds of instructions.
 */
#define ZYREX_TRAMPOLINE_MAX_INSTRUCTION_COUNT_BONUS \
    2

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
 * @brief   Defines the `ZyrexInstructionTranslationType` enum.
 */
typedef enum ZyrexInstructionTranslationType_
{
    /**
     * @brief   This item represents a normal instruction.
     */
    ZYREX_TRANSLATION_TYPE_DEFAULT,
    ///**
    // * @brief   This item represents either the jump instruction from the original code to the
    // *          trampoline or the backjump instruction from the trampoline to the original code.
    // *
    // * Depending on the case, the destination offset or the source offset should be ignored and the respective destination address used instead.
    // */
    //ZYREX_TRANSLATION_TYPE_REDIRECT
} ZyrexInstructionTranslationType;

/**
 * @brief   Defines the `ZyrexInstructionTranslationItem` struct.
 */
typedef struct ZyrexInstructionTranslationItem_
{
    /**
     * @brief   The type of the instruction translation item.
     */
    ZyrexInstructionTranslationType type;
    /**
     * @brief   The offset of a single instruction relative to the beginning of the source buffer.
     */
    ZyanU8 offset_source;
    /**
     * @brief   The offset of a single instruction relative to the beginning of the destination
     *          buffer.
     */
    ZyanU8 offset_destination;
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
    ZyrexInstructionTranslationItem items[ZYREX_TRAMPOLINE_MAX_INSTRUCTION_COUNT + 
                                          ZYREX_TRAMPOLINE_MAX_INSTRUCTION_COUNT_BONUS];
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
     * @brief   Signals, if the trampoline chunk is currently in use.
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
     * @brief   The buffer that holds the trampoline code and the backjump to the hooked function.
     */
    ZyanU8 code_buffer[ZYREX_TRAMPOLINE_MAX_CODE_SIZE_WITH_BACKJUMP + 
                       ZYREX_TRAMPOLINE_MAX_CODE_SIZE_BONUS];
    /**
     * @brief   The number of instruction bytes in the code buffer (not counting the backjump
     *          instruction).
     */
    ZyanU8 code_buffer_size;
    /**
     * @brief   The instruction translation map.
     */
    ZyrexInstructionTranslationMap translation_map;
    /**
     * @brief   The buffer that holds the original instruction bytes saved from the hooked function.
     */
    ZyanU8 original_code[ZYREX_TRAMPOLINE_MAX_CODE_SIZE];
    /**
     * @brief   The number of instruction bytes saved from the hooked function.
     */
    ZyanU8 original_code_size;
} ZyrexTrampolineChunk;

/* ---------------------------------------------------------------------------------------------- */

/* ============================================================================================== */
/* Functions                                                                                      */
/* ============================================================================================== */

/**
 * @brief   Creates a new trampoline.
 *
 * @param   address             The address of the function to create the trampoline for.
 * @param   callback            The address of the callback function the hook will redirect to.
 * @param   min_bytes_to_reloc  Specifies the minimum amount of  bytes that need to be relocated
 *                              to the trampoline (usually equals the size of the branch
 *                              instruction used for hooking).
 *                              This function might copy more bytes on demand to keep individual
 *                              instructions intact.
 * @param   trampoline          Receives the newly created trampoline chunk.
 *
 * @return  A zyan status code.
 */
ZyanStatus ZyrexTrampolineCreate(const void* address, const void* callback,
    ZyanUSize min_bytes_to_reloc, ZyrexTrampolineChunk** trampoline);

/**
 * @brief   Destroys the given trampoline.
 *
 * @param   trampoline  The trampoline chunk.
 *
 * @return  A zyan status code.
 */
ZyanStatus ZyrexTrampolineFree(ZyrexTrampolineChunk* trampoline);

/* ============================================================================================== */

#ifdef __cplusplus
}
#endif

#endif /* ZYREX_INTERNAL_TRAMPOLINE_H */
