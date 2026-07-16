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

#ifndef ENFLAME_DYNLINK_TOPS_CODEC_H
#define ENFLAME_DYNLINK_TOPS_CODEC_H

#include "tops/tops_codec.h"

/* decode functions */
typedef  i32_t ttopscodecDecGetCaps(topscodecType_t codec, u32_t card_id, u32_t device_id, topscodecDecCaps_t *caps);
typedef  i32_t ttopscodecDecCreate(topscodecHandle_t *handle, topscodecDecCreateInfo_t *info);
typedef  i32_t ttopscodecDecSetParams(topscodecHandle_t handle, topscodecDecParams_t *params);
typedef  i32_t ttopscodecDecDestroy(topscodecHandle_t handle);
typedef  i32_t ttopscodecDecodeStream(topscodecHandle_t handle, topscodecStream_t *input, i32_t timeout_ms);
typedef  i32_t ttopscodecDecFrameMap(topscodecHandle_t handle, topscodecFrame_t *frame);
typedef  i32_t ttopscodecDecFrameUnmap(topscodecHandle_t handle, topscodecFrame_t *frame);

#ifdef TOPSCODEC_HAS_ENCODE
/* encode functions */
typedef  i32_t ttopscodecEncGetCaps(topscodecType_t codec, u32_t card_id, u32_t device_id, topscodecEncCaps_t *caps);
typedef  i32_t ttopscodecEncSetParams(topscodecHandle_t handle, topscodecEncParams_t *params);
typedef  i32_t ttopscodecEncOpenEncodeSession(topscodecHandle_t *handle, topscodecEncCreateInfo_t *info);
typedef  i32_t ttopscodecEncDestroy(topscodecHandle_t handle);
typedef  i32_t ttopscodecEncEncodePicture(topscodecHandle_t handle, topscodecFrame_t *frame, topscodecEncPicAttr_t *frame_attr);
typedef  i32_t ttopscodecEncUnlockBitstream(topscodecHandle_t handle, topscodecStream_t *stream);
#endif

/* balance function */
typedef  i32_t ttopscodecSetVideoCoreBalancingPolicy(TOPSCODEC_VIDEO_CORE_BALANCING_POLICY policy);

#endif /* ENFLAME_DYNLINK_TOPS_CODEC_H */
