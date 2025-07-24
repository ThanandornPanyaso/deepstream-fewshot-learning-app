/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __DEEPSTREAM_FEWSHOT_LEARNING_APP_H__
#define __DEEPSTREAM_FEWSHOT_LEARNING_APP_H__

#include <gst/gst.h>
#include "deepstream_config.h"
/** set the user metadata type */
#define NVDS_CUSTOM_IMAGE_PATH_META (nvds_get_user_meta_type("NVIDIA.TRANSFER.IMAGE_PATH_META"))
typedef struct {
  gchar image_path[256];
} NvDsImagePathMeta;
typedef struct
{
  gint anomaly_count;
  gint meta_number;
  struct timespec timespec_first_frame;
  GstClockTime gst_ts_first_frame;
  GMutex lock_stream_rtcp_sr;
  guint32 id;
  gint frameCount;
  GstClockTime last_ntp_time;
  GQueue *frame_embedding_queue;
} StreamSourceInfo;

typedef struct
{
  StreamSourceInfo streams[MAX_SOURCE_BINS];
} TestAppCtx;

typedef struct
{
  gint frame_num;
  GList *obj_embeddings;
} FrameEmbedding;

typedef struct
{
  guint64 object_id;
  guint num_elements;
  float *embedding;
} ObjEmbedding;
#ifdef __cplusplus
extern "C" {
#endif

// Declaration of the function
struct timespec extract_utc_from_uri (gchar * uri);
GstClockTime generate_ts_rfc3339_from_ts(char *buf, int buf_size,
                                                GstClockTime ts, gchar *src_uri,
                                                gint stream_id);
#ifdef __cplusplus
}
#endif



#endif /**< __DEEPSTREAM_TEST5_APP_H__ */
