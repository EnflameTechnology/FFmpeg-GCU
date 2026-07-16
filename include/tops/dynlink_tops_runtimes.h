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

#ifndef ENFLAME_DYNLINK_TOPS_RUNTIMES_H
#define ENFLAME_DYNLINK_TOPS_RUNTIMES_H

#include "tops/tops_runtime_api.h"

typedef topsError_t ttopsSetDevice(int deviceId);
typedef const char* ttopsGetErrorName(topsError_t tops_error);
typedef const char* ttopsGetErrorString(topsError_t topsError);
typedef topsError_t ttopsMalloc(void** ptr, size_t size);
typedef topsError_t ttopsExtMallocWithFlags(void** ptr, size_t sizeBytes, unsigned int flags);
typedef topsError_t ttopsPointerGetAttributes(topsPointerAttribute_t* attributes, const void* ptr);
typedef topsError_t ttopsMemcpy(void* dst, const void* src, size_t sizeBytes, topsMemcpyKind kind);
typedef topsError_t ttopsMemcpyHtoD(topsDeviceptr_t dst, void* src, size_t sizeBytes);
typedef topsError_t ttopsMemcpyDtoH(void* dst, topsDeviceptr_t src, size_t sizeBytes);
typedef topsError_t ttopsMemcpyDtoD(topsDeviceptr_t dst, topsDeviceptr_t src, size_t sizeBytes);
typedef topsError_t ttopsFree(void* ptr);

#endif /* ENFLAME_DYNLINK_TOPS_RUNTIMES_H */
