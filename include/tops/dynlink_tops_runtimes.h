/**
 * Copyright 2020 The Enflame Tech Company. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _DYNLINK_TOPS_RUNTIMES_H_
#define _DYNLINK_TOPS_RUNTIMES_H_

#include "tops/tops_runtime_api.h"
#include "tops/tops_ext.h"

typedef topsError_t ttopsInit(unsigned int flags);
typedef topsError_t ttopsDriverGetVersion(int* driverVersion);
typedef topsError_t ttopsRuntimeGetVersion(int* runtimeVersion);
typedef topsError_t ttopsDeviceGet(topsDevice_t* device, int ordinal);
typedef topsError_t ttopsDeviceComputeCapability(int* major, int* minor, topsDevice_t device);
typedef topsError_t ttopsDeviceGetName(char* name, int len, topsDevice_t device);
typedef topsError_t ttopsDeviceGetPCIBusId(char* pciBusId, int len, int device);
typedef topsError_t ttopsDeviceGetByPCIBusId(int* device, const char* pciBusId);
typedef topsError_t ttopsDeviceTotalMem(size_t* bytes, topsDevice_t device);
typedef topsError_t ttopsDeviceSynchronize(void);
typedef topsError_t ttopsDeviceReset(void);
typedef topsError_t ttopsSetDevice(int deviceId);
typedef topsError_t ttopsGetDevice(int* deviceId);
typedef topsError_t ttopsGetDeviceCount(int* count);
typedef topsError_t ttopsDeviceGetAttribute(int* pi, topsDeviceAttribute_t attr, int deviceId);
typedef topsError_t ttopsGetDeviceProperties(topsDeviceProp_t* prop, int deviceId);
typedef topsError_t ttopsDeviceGetLimit(size_t* pValue, enum topsLimit_t limit);
typedef topsError_t ttopsGetDeviceFlags(unsigned int* flags);
typedef topsError_t ttopsSetDeviceFlags(unsigned flags);
typedef topsError_t ttopsChooseDevice(int* device, const topsDeviceProp_t* prop);
typedef topsError_t ttopsIpcGetMemHandle(topsIpcMemHandle_t* handle, void* devPtr);

typedef topsError_t ttopsGetLastError(void);
typedef topsError_t ttopsPeekAtLastError(void);
typedef const char* ttopsGetErrorName(topsError_t tops_error);
typedef const char* ttopsGetErrorString(topsError_t topsError);

typedef topsError_t ttopsPointerGetAttributes(topsPointerAttribute_t* attributes, const void* ptr);
typedef topsError_t ttopsPointerGetAttribute(void* data, topsPointer_attribute attribute,topsDeviceptr_t ptr);
typedef topsError_t ttopsDrvPointerGetAttributes(unsigned int numAttributes, topsPointer_attribute* attributes, void** data, topsDeviceptr_t ptr);
typedef topsError_t ttopsMalloc(void** ptr, size_t size);
typedef topsError_t ttopsExtCodecMemHandle(void** pointer, uint64_t dev_addr, size_t size);
typedef topsError_t ttopsExtMallocWithFlags(void** ptr, size_t sizeBytes, unsigned int flags);
typedef topsError_t ttopsHostMalloc(void** ptr, size_t size, unsigned int flags);
typedef topsError_t ttopsHostGetDevicePointer(void** devPtr, void* hostPtr, unsigned int flags);
typedef topsError_t ttopsHostGetFlags(unsigned int* flagsPtr, void* hostPtr);
typedef topsError_t ttopsHostRegister(void* hostPtr, size_t sizeBytes, unsigned int flags);
typedef topsError_t ttopsHostUnregister(void* hostPtr);
typedef topsError_t ttopsFree(void* ptr);
typedef topsError_t ttopsHostFree(void* ptr);
typedef topsError_t ttopsMemcpy(void* dst, const void* src, size_t sizeBytes, topsMemcpyKind kind);
typedef topsError_t ttopsMemcpyWithStream(void* dst, const void* src, size_t sizeBytes,topsMemcpyKind kind, topsStream_t stream);
typedef topsError_t ttopsMemcpyHtoD(topsDeviceptr_t dst, void* src, size_t sizeBytes);
typedef topsError_t ttopsMemcpyDtoH(void* dst, topsDeviceptr_t src, size_t sizeBytes);
typedef topsError_t ttopsMemcpyDtoD(topsDeviceptr_t dst, topsDeviceptr_t src, size_t sizeBytes);
typedef topsError_t ttopsMemcpyHtoDAsync(topsDeviceptr_t dst, void* src, size_t sizeBytes, topsStream_t stream);
typedef topsError_t ttopsMemcpyDtoHAsync(void* dst, topsDeviceptr_t src, size_t sizeBytes, topsStream_t stream);
typedef topsError_t ttopsMemcpyDtoDAsync(topsDeviceptr_t dst, topsDeviceptr_t src, size_t sizeBytes,topsStream_t stream);
typedef topsError_t ttopsMemset(void* dst, int value, size_t sizeBytes);

//tops_ext.h
typedef topsError_t ttopsExtMallocWithAffinity(void **ptr, size_t size,uint64_t bank, unsigned int flags);
                                                              
#endif //_DYNLINK_TOPS_RUNTIMES_H_