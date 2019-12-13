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

/**
 * @file
 * @brief   Master include file, including everything else.
 */

#ifndef ZYREX_H
#define ZYREX_H

#include <ZyrexExportConfig.h>
#include <Zycore/Status.h>
#include <Zycore/Types.h>

// TODO:

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================================== */
/* Macros                                                                                         */
/* ============================================================================================== */

/* ---------------------------------------------------------------------------------------------- */
/* Constants                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   A macro that defines the zyrex version.
 */
#define ZYREX_VERSION (ZyanU64)0x0001000000000000

/* ---------------------------------------------------------------------------------------------- */
/* Helper macros                                                                                  */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Extracts the major-part of the zyrex version.
 *
 * @param   version The zyrex version value
 */
#define ZYREX_VERSION_MAJOR(version) (ZyanU16)(((version) & 0xFFFF000000000000) >> 48)

/**
 * @brief   Extracts the minor-part of the zyrex version.
 *
 * @param   version The zyrex version value
 */
#define ZYREX_VERSION_MINOR(version) (ZyanU16)(((version) & 0x0000FFFF00000000) >> 32)

/**
 * @brief   Extracts the patch-part of the zyrex version.
 *
 * @param   version The zyrex version value
 */
#define ZYREX_VERSION_PATCH(version) (ZyanU16)(((version) & 0x00000000FFFF0000) >> 16)

/**
 * @brief   Extracts the build-part of the zyrex version.
 *
 * @param   version The zyrex version value
 */
#define ZYREX_VERSION_BUILD(version) (ZyanU16)((version) & 0x000000000000FFFF)

/* ---------------------------------------------------------------------------------------------- */

/* ============================================================================================== */
/* Exported functions                                                                             */
/* ============================================================================================== */

/* ---------------------------------------------------------------------------------------------- */
/* Initialization & Finalization                                                                  */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Initializes the `Zyrex` hook engine.
 *
 * @return  A zyan status code.
 *
 * This function has to be called before invoking any other `Zyrex*` API.
 */
ZYREX_EXPORT ZyanStatus ZyrexInitialize(void);

/**
 * @brief   Releases global resources allocated by the `Zyrex` hook engine.
 *
 * @return  A zyan status code.
 *
 * No `Zyrex*` API function should be called after invoking this function.
 */
ZYREX_EXPORT ZyanStatus ZyrexShutdown(void);

/* ---------------------------------------------------------------------------------------------- */
/* Information                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

/**
 * @brief   Returns the zyrex version.
 *
 * @return  The zyrex version.
 *
 * Use the macros provided in this file to extract the major, minor, patch and build part from the
 * returned version value.
 */
ZYREX_EXPORT ZyanU64 ZyrexGetVersion(void);

/* ---------------------------------------------------------------------------------------------- */

/* ============================================================================================== */

#ifdef __cplusplus
}
#endif

#endif /* ZYREX_H */
