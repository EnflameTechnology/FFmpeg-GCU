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

#ifndef _DYNLINK_TOPS_CODEC_H_
#define _DYNLINK_TOPS_CODEC_H_

#include "tops/tops_codec.h"

typedef  i32_t ttopscodecGetLibVersion(u32_t *major,u32_t *minor,u32_t *patch);
typedef  i32_t ttopscodecGetMemoryHandle(u64_t dev_addr,u64_t size, void** rt_handle);
typedef  i32_t ttopscodecDecGetCaps(topscodecType_t codec, u32_t card_id, u32_t device_id, topscodecDecCaps_t *caps);
typedef  i32_t ttopscodecDecCreate(topscodecHandle_t *handle, topscodecDecCreateInfo_t *info);
typedef  i32_t ttopscodecDecSetParams(topscodecHandle_t handle, topscodecDecParams_t *params);
typedef  i32_t ttopscodecDecDestroy(topscodecHandle_t handle);
typedef  i32_t ttopscodecDecodeStream(topscodecHandle_t handle, topscodecStream_t *input, i32_t timeout_ms);
typedef  i32_t ttopscodecDecFrameMap(topscodecHandle_t handle, topscodecFrame_t *frame);
typedef  i32_t ttopscodecDecFrameUnmap(topscodecHandle_t handle, topscodecFrame_t *frame);
typedef  i32_t ttopscodecDecGetStatus(topscodecHandle_t handle, topscodecDecStatus_t *status);
typedef  i32_t ttopscodecGetLoading(topscodecDevID_t deviceID, u32_t sess_id, double *loading);

#endif //_DYNLINK_TOPS_CODEC_H_