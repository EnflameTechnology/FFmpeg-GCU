/*
 * The confidential and proprietary information contained in this file may
 * only be used by a person authorised under and to the extent permitted
 * by a subsisting licensing agreement from Enflame Tech.Co., Ltd.
 *
 *            (C) COPYRIGHT 2022-2026 Enflame Tech.Co., Ltd.
 *                ALL RIGHTS RESERVED
 *
 * This entire notice must be reproduced on all copies of this file
 * and copies of this file may only be made by a person if such person is
 * permitted to do so under the terms of a subsisting license agreement
 * from Enflame Tech.Co., Ltd.
 */
#ifndef ENFLAME_DYNLINK_TOPS_LOADER_H
#define ENFLAME_DYNLINK_TOPS_LOADER_H

#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <pthread.h>
#include "dynlink_tops_codec.h"
#include "dynlink_tops_runtimes.h"

#define TOPSCODEC_LIBNAME     "libtopscodec.so"
#define TOPS_RUNTIMES_LIBNAME "libtopsrt.so"

#define TOPS_LIB_HANDLE void*
#define TOPS_SYM_FUNC(lib, sym) dlsym((lib), (sym))

extern pthread_mutex_t av_tops_lib_mutex;

static inline void* tops_dlopen_locked(const char *path) {
    void *handle;
    pthread_mutex_lock(&av_tops_lib_mutex);
    handle = dlopen(path, RTLD_LAZY);
    pthread_mutex_unlock(&av_tops_lib_mutex);
    return handle;
}

static inline void tops_dlclose_locked(void *handle) {
    pthread_mutex_lock(&av_tops_lib_mutex);
    dlclose(handle);
    pthread_mutex_unlock(&av_tops_lib_mutex);
}

#define TOPS_LOAD_FUNC(path) tops_dlopen_locked(path)
#define TOPS_FREE_FUNC(lib) tops_dlclose_locked(lib)

enum dyn_debug_level {
  DYN_DEBUG_LEVEL_DISABLE = 0,
  DYN_DEBUG_LEVEL_ERR,
  DYN_DEBUG_LEVEL_INFO,
  DYN_DEBUG_LEVEL_DEBUG
};
static atomic_int dynlink_efdebug = DYN_DEBUG_LEVEL_ERR;
static void dynlink_set_debug_level(int level)
{
    dynlink_efdebug = level;
}

#define PRINT printf
#define DYN_TOPS_LOG_FUNC_ERR(...)                                            \
  do {                                                                        \
    PRINT(__VA_ARGS__);                                                       \
  } while (0)

#define DYN_TOPS_LOG_FUNC_INFO(...)                                           \
  do {                                                                        \
    if (dynlink_efdebug >= DYN_DEBUG_LEVEL_INFO) {                            \
      PRINT(__VA_ARGS__);                                                     \
    }                                                                         \
  } while (0)

#define DYN_TOPS_LOG_FUNC_DEBUG(...)                                          \
  do {                                                                        \
      if (dynlink_efdebug >= DYN_DEBUG_LEVEL_DEBUG) {                         \
        PRINT(__VA_ARGS__);                                                   \
      }                                                                       \
  } while (0)

#define LOAD_LIBRARY(l, path)                                                 \
    do {                                                                      \
        if (!((l) = TOPS_LOAD_FUNC(path))) {                                  \
            DYN_TOPS_LOG_FUNC_ERR("Cannot load %s\n", path);                  \
            ret = -1;                                                         \
            goto error;                                                       \
        }                                                                     \
        DYN_TOPS_LOG_FUNC_INFO("Loaded lib: %s\n", path);                     \
    } while (0)

#define LOAD_SYMBOL(fun, tp, symbol)                                          \
    do {                                                                      \
        if (!((f->fun) = (tp*)TOPS_SYM_FUNC(f->lib, symbol))) {               \
            DYN_TOPS_LOG_FUNC_ERR("Cannot load symbol: %s\n", symbol);        \
            ret = -1;                                                         \
            goto error;                                                       \
        }                                                                     \
        DYN_TOPS_LOG_FUNC_INFO("Loaded sym: %s\n", symbol);                   \
    } while (0)

#define LOAD_SYMBOL_OPT(fun, tp, symbol)                                      \
    do {                                                                      \
        if (!((f->fun) = (tp*)TOPS_SYM_FUNC(f->lib, symbol))) {               \
            DYN_TOPS_LOG_FUNC_INFO("Optional symbol not found: %s\n", symbol);\
        } else {                                                              \
            DYN_TOPS_LOG_FUNC_DEBUG("Loaded sym: %s\n", symbol);              \
        }                                                                     \
    } while (0)

#define GENERIC_LOAD_FUNC_PREAMBLE(T, n, N)                                   \
    T *f;                                                                     \
    int ret;                                                                  \
                                                                              \
    n##_free_functions(functions);                                            \
                                                                              \
    f = *functions = (T*)calloc(1, sizeof(*f));                               \
    if (!f)                                                                   \
        return -1;                                                            \
                                                                              \
    LOAD_LIBRARY(f->lib, N);

#define GENERIC_LOAD_FUNC_FINALE(n)                                           \
    return 0;                                                                 \
error:                                                                        \
    n##_free_functions(functions);                                            \
    return ret;

#define GENERIC_FREE_FUNC()                                                   \
    if (!functions)                                                           \
        return;                                                               \
    if (*functions && (*functions)->lib)                                      \
        TOPS_FREE_FUNC((*functions)->lib);                                    \
    free(*functions);                                                         \
    *functions = NULL;


/* topscodec function definition */
typedef struct TopsCodecFunctions_t {
    /* decode functions (mandatory) */
    ttopscodecDecGetCaps      *lib_topscodecDecGetCaps;
    ttopscodecDecCreate       *lib_topscodecDecCreate;
    ttopscodecDecSetParams    *lib_topscodecDecSetParams;
    ttopscodecDecDestroy      *lib_topscodecDecDestroy;
    ttopscodecDecodeStream    *lib_topscodecDecodeStream;
    ttopscodecDecFrameMap     *lib_topscodecDecFrameMap;
    ttopscodecDecFrameUnmap   *lib_topscodecDecFrameUnmap;

#ifdef TOPSCODEC_HAS_ENCODE
    /* encode functions (optional) */
    ttopscodecEncGetCaps            *lib_topscodecEncGetCaps;
    ttopscodecEncSetParams          *lib_topscodecEncSetParams;
    ttopscodecEncOpenEncodeSession  *lib_topscodecEncOpenEncodeSession;
    ttopscodecEncDestroy            *lib_topscodecEncDestroy;
    ttopscodecEncEncodePicture      *lib_topscodecEncEncodePicture;
    ttopscodecEncUnlockBitstream    *lib_topscodecEncUnlockBitstream;
#endif

    /* balance function (mandatory) */
    ttopscodecSetVideoCoreBalancingPolicy *lib_topscodecSetVideoCoreBalancingPolicy;

    TOPS_LIB_HANDLE lib;
} TopsCodecFunctions;

typedef struct TopsRuntimesFunctions_t {
    ttopsSetDevice            *lib_topsSetDevice;
    ttopsGetErrorName         *lib_topsGetErrorName;
    ttopsGetErrorString       *lib_topsGetErrorString;
    ttopsMalloc               *lib_topsMalloc;
    ttopsExtMallocWithFlags   *lib_topsExtMallocWithFlags;
    ttopsPointerGetAttributes *lib_topsPointerGetAttributes;
    ttopsMemcpy               *lib_topsMemcpy;
    ttopsMemcpyHtoD           *lib_topsMemcpyHtoD;
    ttopsMemcpyDtoH           *lib_topsMemcpyDtoH;
    ttopsMemcpyDtoD           *lib_topsMemcpyDtoD;
    ttopsFree                 *lib_topsFree;

    TOPS_LIB_HANDLE lib;
} TopsRuntimesFunctions;

static inline void topscodec_free_functions(TopsCodecFunctions **functions)
{
    GENERIC_FREE_FUNC();
}

static inline void topsruntimes_free_functions(TopsRuntimesFunctions **functions)
{
    GENERIC_FREE_FUNC();
}

static inline int topscodec_load_functions(TopsCodecFunctions **functions)
{
    GENERIC_LOAD_FUNC_PREAMBLE(TopsCodecFunctions, topscodec, TOPSCODEC_LIBNAME);

    /* decode functions (mandatory) */
    LOAD_SYMBOL(lib_topscodecDecGetCaps,      ttopscodecDecGetCaps,      "topscodecDecGetCaps");
    LOAD_SYMBOL(lib_topscodecDecCreate,       ttopscodecDecCreate,       "topscodecDecCreate");
    LOAD_SYMBOL(lib_topscodecDecSetParams,    ttopscodecDecSetParams,    "topscodecDecSetParams");
    LOAD_SYMBOL(lib_topscodecDecDestroy,      ttopscodecDecDestroy,      "topscodecDecDestroy");
    LOAD_SYMBOL(lib_topscodecDecodeStream,    ttopscodecDecodeStream,    "topscodecDecodeStream");
    LOAD_SYMBOL(lib_topscodecDecFrameMap,     ttopscodecDecFrameMap,     "topscodecDecFrameMap");
    LOAD_SYMBOL(lib_topscodecDecFrameUnmap,   ttopscodecDecFrameUnmap,   "topscodecDecFrameUnmap");

#ifdef TOPSCODEC_HAS_ENCODE
    /* encode functions (optional) */
    LOAD_SYMBOL_OPT(lib_topscodecEncGetCaps,           ttopscodecEncGetCaps,           "topscodecEncGetCaps");
    LOAD_SYMBOL_OPT(lib_topscodecEncSetParams,         ttopscodecEncSetParams,         "topscodecEncSetParams");
    LOAD_SYMBOL_OPT(lib_topscodecEncOpenEncodeSession, ttopscodecEncOpenEncodeSession, "topscodecEncOpenEncodeSession");
    LOAD_SYMBOL_OPT(lib_topscodecEncDestroy,           ttopscodecEncDestroy,           "topscodecEncDestroy");
    LOAD_SYMBOL_OPT(lib_topscodecEncEncodePicture,     ttopscodecEncEncodePicture,     "topscodecEncEncodePicture");
    LOAD_SYMBOL_OPT(lib_topscodecEncUnlockBitstream,   ttopscodecEncUnlockBitstream,   "topscodecEncUnlockBitstream");
#endif

    /* balance (mandatory) */
    LOAD_SYMBOL(lib_topscodecSetVideoCoreBalancingPolicy, ttopscodecSetVideoCoreBalancingPolicy, "topscodecSetVideoCoreBalancingPolicy");

    GENERIC_LOAD_FUNC_FINALE(topscodec);
}

static inline int topsruntimes_load_functions(TopsRuntimesFunctions **functions)
{
    GENERIC_LOAD_FUNC_PREAMBLE(TopsRuntimesFunctions, topsruntimes, TOPS_RUNTIMES_LIBNAME);

    LOAD_SYMBOL(lib_topsSetDevice,            ttopsSetDevice,            "topsSetDevice");
    LOAD_SYMBOL(lib_topsGetErrorName,         ttopsGetErrorName,         "topsGetErrorName");
    LOAD_SYMBOL(lib_topsGetErrorString,       ttopsGetErrorString,       "topsGetErrorString");
    LOAD_SYMBOL(lib_topsMalloc,               ttopsMalloc,               "topsMalloc");
    LOAD_SYMBOL(lib_topsExtMallocWithFlags,   ttopsExtMallocWithFlags,   "topsExtMallocWithFlags");
    LOAD_SYMBOL(lib_topsPointerGetAttributes, ttopsPointerGetAttributes, "topsPointerGetAttributes");
    LOAD_SYMBOL(lib_topsMemcpy,               ttopsMemcpy,               "topsMemcpy");
    LOAD_SYMBOL(lib_topsMemcpyHtoD,           ttopsMemcpyHtoD,           "topsMemcpyHtoD");
    LOAD_SYMBOL(lib_topsMemcpyDtoH,           ttopsMemcpyDtoH,           "topsMemcpyDtoH");
    LOAD_SYMBOL(lib_topsMemcpyDtoD,           ttopsMemcpyDtoD,           "topsMemcpyDtoD");
    LOAD_SYMBOL(lib_topsFree,                 ttopsFree,                 "topsFree");

    GENERIC_LOAD_FUNC_FINALE(topsruntimes);
}

static inline int tops_runtimes_check(void *topsGetErrorName_fn,
                                      void *topsGetErrorString_fn,
                                      topsError_t err, const char *func)
{
    const char *err_name;
    const char *err_string;

    DYN_TOPS_LOG_FUNC_DEBUG("Calling %s\n", func);

    if (err == topsSuccess)
        return 0;

    err_name = ((ttopsGetErrorName *)topsGetErrorName_fn)(err);
    err_string = ((ttopsGetErrorString *)topsGetErrorString_fn)(err);

    DYN_TOPS_LOG_FUNC_ERR("%s failed", func);
    if (err_name && err_string)
        DYN_TOPS_LOG_FUNC_ERR(" -> %s: %s", err_name, err_string);

    DYN_TOPS_LOG_FUNC_ERR("\n");
    return err;
}


#define TOPS_CHECK_LIB(topsl, x)                               \
            tops_runtimes_check(topsl->lib_topsGetErrorName,   \
                          topsl->lib_topsGetErrorString,       \
                          (x), #x)

#endif /* ENFLAME_DYNLINK_TOPS_LOADER_H */
