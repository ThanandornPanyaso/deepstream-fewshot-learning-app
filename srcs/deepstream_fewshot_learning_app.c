/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2023 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: MIT
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

#include "deepstream_fewshot_learning_app.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cuda_runtime_api.h>
#include <errno.h>
#include <glib.h>
#include <gst/gst.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
// #include <map>
// #include <vector>

#include "analytics.h"
#include "deepstream_app.h"
#include "deepstream_config_file_parser.h"
#include "glibconfig.h"
#include "gstnvdsmeta.h"
#include "image_meta_consumer_wrapper.h"
#include "nvds_tracker_meta.h"
#include "nvds_version.h"
#include "nvdsmeta_schema.h"
// #include "image_meta_producer_wrapper.h"

/**
 * Logging levels
 */
#define LOG_LVL_FATAL 0
#define LOG_LVL_ERROR 1
#define LOG_LVL_WARN 2
#define LOG_LVL_INFO 3
#define LOG_LVL_DEBUG 4

#define MAX_DISPLAY_LEN (64)
#define MAX_TIME_STAMP_LEN (64)
#define STREAMMUX_BUFFER_POOL_SIZE (16)

#define INOTIFY_EVENT_SIZE (sizeof(struct inotify_event))
#define INOTIFY_EVENT_BUF_LEN (1024 * (INOTIFY_EVENT_SIZE + 16))

#define IS_YAML(file)                                                          \
  (g_str_has_suffix(file, ".yml") || g_str_has_suffix(file, ".yaml"))

/** @{
 * Macro's below and corresponding code-blocks are used to demonstrate
 * nvmsgconv + Broker Metadata manipulation possibility
 */

/**
 * IMPORTANT Note 1:
 * The code within the check for model_used ==
 * APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE is applicable as
 * sample demo code for configs that use resnet PGIE model with class ID's: {0,
 * 1, 2, 3} for {CAR, BICYCLE, PERSON, ROADSIGN} followed by optional Tracker +
 * 3 X SGIEs (Vehicle-Type,Color,Make) only! Please comment out the code if
 * using any other custom PGIE + SGIE combinations and use the code as reference
 * to write your own NvDsEventMsgMeta generation code in
 * generate_event_msg_meta() function
 */
typedef enum {
  APP_CONFIG_ANALYTICS_FSL = 0,
  APP_CONFIG_ANALYTICS_MTMC = 1,
  APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE = 2,
  APP_CONFIG_ANALYTICS_MODELS_UNKNOWN = 3,
} AppConfigAnalyticsModel;

/**
 * IMPORTANT Note 2:
 * GENERATE_DUMMY_META_EXT macro implements code
 * that assumes APP_CONFIG_ANALYTICS_RESNET_PGIE_3SGIE_TYPE_COLOR_MAKE
 * case discussed above, and generate dummy metadata
 * for other classes like Person class
 *
 * Vehicle class schema meta (NvDsVehicleObject) is filled
 * in properly from Classifier-Metadata;
 * see in-code documentation and usage of
 * schema_fill_sample_sgie_vehicle_metadata()
 */
#define GENERATE_DUMMY_META_EXT

/** Following class-ID's
 * used for demonstration code
 * assume an ITS detection model
 * which outputs CLASS_ID=0 for Vehicle class
 * and CLASS_ID=2 for Person class
 * and SGIEs X 3 same as the sample DS config for test5-app:
 * configs/test5_config_file_src_infer_tracker_sgie.txt
 */

#define SECONDARY_GIE_VEHICLE_TYPE_UNIQUE_ID (4)
#define SECONDARY_GIE_VEHICLE_COLOR_UNIQUE_ID (5)
#define SECONDARY_GIE_VEHICLE_MAKE_UNIQUE_ID (6)

#define RESNET10_PGIE_3SGIE_TYPE_COLOR_MAKECLASS_ID_CAR (0)
#ifdef GENERATE_DUMMY_META_EXT
#define RESNET10_PGIE_3SGIE_TYPE_COLOR_MAKECLASS_ID_PERSON (2)
#endif
/** @} */

#ifdef EN_DEBUG
#define LOGD(...) printf(__VA_ARGS__)
#else
#define LOGD(...)
#endif

static TestAppCtx *testAppCtx;
GST_DEBUG_CATEGORY(NVDS_APP);

/** @{ imported from deepstream-app as is */

#define MAX_INSTANCES 128
#define APP_TITLE "DeepStreamFslApp"

#define DEFAULT_X_WINDOW_WIDTH 1920
#define DEFAULT_X_WINDOW_HEIGHT 1080

AppCtx *appCtx[MAX_INSTANCES];
static guint cintr = FALSE;
static GMainLoop *main_loop = NULL;
static gchar **cfg_files = NULL;
static gchar **input_files = NULL;
static gchar **override_cfg_file = NULL;
static gboolean playback_utc = FALSE;
static gboolean print_version = FALSE;
static gboolean show_bbox_text = FALSE;
static gboolean force_tcp = TRUE;
static gboolean print_dependencies_version = FALSE;
static gboolean quit = FALSE;
static gboolean use_tracker_reid = FALSE;
static guint tracker_reid_store_age = 0;
static gint return_value = 0;
static guint num_instances;
static guint num_input_files;
static GMutex fps_lock;
static gdouble fps[MAX_SOURCE_BINS];
static gdouble fps_avg[MAX_SOURCE_BINS];
// static std::map<int, int> reid_cache;

static Display *display = NULL;
static Window windows[MAX_INSTANCES] = {0};

static GThread *x_event_thread = NULL;
static GMutex disp_lock;

static guint rrow, rcol, rcfg;
static gboolean rrowsel = FALSE, selecting = FALSE;
static AppConfigAnalyticsModel model_used = APP_CONFIG_ANALYTICS_MODELS_UNKNOWN;
static gint log_level = 0;
static gint message_rate = 30;
static ImageMetaConsumerWrapper *g_img_meta_consumer;
static struct timeval ota_request_time;
static struct timeval ota_completion_time;

typedef struct _OTAInfo {
  AppCtx *appCtx;
  gchar *override_cfg_file;
} OTAInfo;

// static gint include_fps = 0;
static gint target_class = 0;

/** @} imported from deepstream-app as is */
GOptionEntry entries[] = {
    {"version", 'v', 0, G_OPTION_ARG_NONE, &print_version,
     "Print DeepStreamSDK version", NULL},
    {"tiledtext", 'b', 0, G_OPTION_ARG_NONE, &show_bbox_text,
     "Display Bounding box labels in tiled mode", NULL},
    {"version-all", 0, 0, G_OPTION_ARG_NONE, &print_dependencies_version,
     "Print DeepStreamSDK and dependencies version", NULL},
    {"cfg-file", 'c', 0, G_OPTION_ARG_FILENAME_ARRAY, &cfg_files,
     "Set the config file", NULL},
    {"override-cfg-file", 'o', 0, G_OPTION_ARG_FILENAME_ARRAY,
     &override_cfg_file,
     "Set the override config file, used for on-the-fly model update feature",
     NULL},
    {"input-file", 'i', 0, G_OPTION_ARG_FILENAME_ARRAY, &input_files,
     "Set the input file", NULL},
    {"playback-utc", 'p', 0, G_OPTION_ARG_INT, &playback_utc,
     "Playback utc; default=false (base UTC from file-URL or RTCP Sender "
     "Report) =true (base UTC from file/rtsp URL)",
     NULL},
    {"pgie-model-used", 'm', 0, G_OPTION_ARG_INT, &model_used,
     "PGIE Model used; {0: FSL}, {1: MTMC}, {2: Resnet 4-class [Car, "
     "Bicycle, Person, Roadsign]}, {3 - Unknown [DEFAULT]}",
     NULL},
    {"no-force-tcp", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &force_tcp,
     "Do not force TCP for RTP transport", NULL},
    {"log-level", 'l', 0, G_OPTION_ARG_INT, &log_level,
     "Log level for prints, default=0", NULL},
    {"message-rate", 'r', 0, G_OPTION_ARG_INT, &message_rate,
     "Message rate for broker", NULL},
    {"target-class", 't', 0, G_OPTION_ARG_INT, &target_class,
     "Target class for MTMC", NULL},
    {"tracker-reid", 0, 0, G_OPTION_ARG_NONE, &use_tracker_reid,
     "Use tracker re-identification as embedding", NULL},
    {"reid-store-age", 0, 0, G_OPTION_ARG_INT, &tracker_reid_store_age,
     "Tracker reid store age", NULL},
    {NULL},
};

/**
 * @brief  Fill NvDsVehicleObject with the NvDsClassifierMetaList
 *         information in NvDsObjectMeta
 *         NOTE: This function assumes the test-application is
 *         run with 3 X SGIEs sample config:
 *         test5_config_file_src_infer_tracker_sgie.txt
 *         or an equivalent config
 *         NOTE: If user is adding custom SGIEs, make sure to
 *         edit this function implementation
 * @param  obj_params [IN] The NvDsObjectMeta as detected and kept
 *         in NvDsBatchMeta->NvDsFrameMeta(List)->NvDsObjectMeta(List)
 * @param  obj [IN/OUT] The NvDSMeta-Schema defined Vehicle metadata
 *         structure
 */
static void schema_fill_sample_sgie_vehicle_metadata(NvDsObjectMeta *obj_params,
                                                     NvDsVehicleObject *obj);

/**
 * @brief  Performs model update OTA operation
 *         Sets "model-engine-file" configuration parameter
 *         on infer plugin to initiate model switch OTA process
 * @param  ota_appCtx [IN] App context pointer
 */
void apply_ota(AppCtx *ota_appCtx);

/**
 * @brief  Thread which handles the model-update OTA functionlity
 *         1) Adds watch on the changes made in the provided ota-override-file,
 *            if changes are detected, validate the model-update change request,
 *            intiate model-update OTA process
 *         2) Frame drops / frames without inference should NOT be detected in
 *            this on-the-fly model update process
 *         3) In case of model update OTA fails, error message will be printed
 *            on the console and pipeline continues to run with older
 *            model configuration
 * @param  gpointer [IN] Pointer to OTAInfo structure
 * @param  gpointer [OUT] Returns NULL in case of thread exits
 */
gpointer ota_handler_thread(gpointer data);

static void generate_ts_rfc3339(char *buf, int buf_size) {
  time_t tloc;
  struct tm tm_log;
  struct timespec ts;
  char strmsec[6]; //.nnnZ\0

  clock_gettime(CLOCK_REALTIME, &ts);
  memcpy(&tloc, (void *)(&ts.tv_sec), sizeof(time_t));
  gmtime_r(&tloc, &tm_log);
  strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%S", &tm_log);
  int ms = ts.tv_nsec / 1000000;
  g_snprintf(strmsec, sizeof(strmsec), ".%.3dZ", ms);
  strncat(buf, strmsec, buf_size);
}

GstClockTime generate_ts_rfc3339_from_ts(char *buf, int buf_size,
                                         GstClockTime ts, gchar *src_uri,
                                         gint stream_id) {
  time_t tloc;
  struct tm tm_log;
  char strmsec[6]; //.nnnZ\0
  int ms;

  GstClockTime ts_generated;

  if (playback_utc || (appCtx[0]->config.multi_source_config[stream_id].type !=
                       NV_DS_SOURCE_RTSP)) {
    if (testAppCtx->streams[stream_id].meta_number == 0) {
      testAppCtx->streams[stream_id].timespec_first_frame =
          extract_utc_from_uri(src_uri);
      memcpy(
          &tloc,
          (void *)(&testAppCtx->streams[stream_id].timespec_first_frame.tv_sec),
          sizeof(time_t));
      ms =
          testAppCtx->streams[stream_id].timespec_first_frame.tv_nsec / 1000000;
      testAppCtx->streams[stream_id].gst_ts_first_frame = ts;
      ts_generated = GST_TIMESPEC_TO_TIME(
          testAppCtx->streams[stream_id].timespec_first_frame);
      if (ts_generated == 0) {
        if (log_level >= LOG_LVL_WARN) {
          g_print(
              "WARNING; playback mode used with URI [%s] not conforming to "
              "timestamp format;"
              " check README; using system-time\n",
              src_uri);
        }
        clock_gettime(CLOCK_REALTIME,
                      &testAppCtx->streams[stream_id].timespec_first_frame);
        ts_generated = GST_TIMESPEC_TO_TIME(
            testAppCtx->streams[stream_id].timespec_first_frame);
      }
    } else {
      GstClockTime ts_current =
          GST_TIMESPEC_TO_TIME(
              testAppCtx->streams[stream_id].timespec_first_frame) +
          (ts - testAppCtx->streams[stream_id].gst_ts_first_frame);
      struct timespec timespec_current;
      GST_TIME_TO_TIMESPEC(ts_current, timespec_current);
      memcpy(&tloc, (void *)(&timespec_current.tv_sec), sizeof(time_t));
      ms = timespec_current.tv_nsec / 1000000;
      ts_generated = ts_current;
    }
  } else {
    /** ts itself is UTC Time in ns */
    struct timespec timespec_current;
    GST_TIME_TO_TIMESPEC(ts, timespec_current);
    memcpy(&tloc, (void *)(&timespec_current.tv_sec), sizeof(time_t));
    ms = timespec_current.tv_nsec / 1000000;
    ts_generated = ts;
  }
  gmtime_r(&tloc, &tm_log);
  strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%S", &tm_log);
  g_snprintf(strmsec, sizeof(strmsec), ".%.3dZ", ms);
  strncat(buf, strmsec, buf_size);
  if (log_level >= LOG_LVL_DEBUG) {
    LOGD("ts=%s\n", buf);
  }
  // g_print("Generate time=%s, id=%d\n", buf, stream_id);
  return ts_generated;
}

static gpointer meta_copy_func(gpointer data, gpointer user_data) {
  NvDsUserMeta *user_meta = (NvDsUserMeta *)data;
  NvDsEventMsgMeta *srcMeta = (NvDsEventMsgMeta *)user_meta->user_meta_data;
  NvDsEventMsgMeta *dstMeta = NULL;

  dstMeta = (NvDsEventMsgMeta *)g_memdup(srcMeta, sizeof(NvDsEventMsgMeta));

  if (srcMeta->ts) dstMeta->ts = g_strdup(srcMeta->ts);

  if (srcMeta->objSignature.size > 0) {
    dstMeta->objSignature.signature = (gdouble *)g_memdup(
        srcMeta->objSignature.signature, srcMeta->objSignature.size);
    dstMeta->objSignature.size = srcMeta->objSignature.size;
  }

  if (srcMeta->objectId) {
    dstMeta->objectId = g_strdup(srcMeta->objectId);
  }

  if (srcMeta->sensorStr) {
    dstMeta->sensorStr = g_strdup(srcMeta->sensorStr);
  }

  if (srcMeta->extMsgSize > 0) {
    if (srcMeta->objType == NVDS_OBJECT_TYPE_VEHICLE) {
      NvDsVehicleObject *srcObj = (NvDsVehicleObject *)srcMeta->extMsg;
      NvDsVehicleObject *obj =
          (NvDsVehicleObject *)g_malloc0(sizeof(NvDsVehicleObject));
      if (srcObj->type) obj->type = g_strdup(srcObj->type);
      if (srcObj->make) obj->make = g_strdup(srcObj->make);
      if (srcObj->model) obj->model = g_strdup(srcObj->model);
      if (srcObj->color) obj->color = g_strdup(srcObj->color);
      if (srcObj->license) obj->license = g_strdup(srcObj->license);
      if (srcObj->region) obj->region = g_strdup(srcObj->region);

      dstMeta->extMsg = obj;
      dstMeta->extMsgSize = sizeof(NvDsVehicleObject);
    } else if (srcMeta->objType == NVDS_OBJECT_TYPE_PERSON) {
      NvDsPersonObject *srcObj = (NvDsPersonObject *)srcMeta->extMsg;
      NvDsPersonObject *obj =
          (NvDsPersonObject *)g_malloc0(sizeof(NvDsPersonObject));

      obj->age = srcObj->age;

      if (srcObj->gender) obj->gender = g_strdup(srcObj->gender);
      if (srcObj->cap) obj->cap = g_strdup(srcObj->cap);
      if (srcObj->hair) obj->hair = g_strdup(srcObj->hair);
      if (srcObj->apparel) obj->apparel = g_strdup(srcObj->apparel);

      dstMeta->extMsg = obj;
      dstMeta->extMsgSize = sizeof(NvDsPersonObject);
    }
    //! Extensions for Fewshot Learning
    else if (srcMeta->objType == NVDS_OBJECT_TYPE_PRODUCT) {
      NvDsProductObject *srcObj = (NvDsProductObject *)srcMeta->extMsg;
      NvDsProductObject *obj =
          (NvDsProductObject *)g_malloc0(sizeof(NvDsProductObject));
      if (srcObj->brand) obj->brand = g_strdup(srcObj->brand);
      if (srcObj->type) obj->type = g_strdup(srcObj->type);
      if (srcObj->shape) obj->shape = g_strdup(srcObj->shape);

      dstMeta->extMsg = obj;
      dstMeta->extMsgSize = sizeof(NvDsProductObject);
    }
  }

  if (srcMeta->embedding.embedding_length > 0) {
    dstMeta->embedding.embedding_length = srcMeta->embedding.embedding_length;
    dstMeta->embedding.embedding_vector =
        g_memdup(srcMeta->embedding.embedding_vector,
                 srcMeta->embedding.embedding_length * sizeof(float));
  }

  if (srcMeta->has3DTracking) {
    dstMeta->has3DTracking = true;
    dstMeta->singleView3DTracking.visibility = srcMeta->singleView3DTracking.visibility;
    dstMeta->singleView3DTracking.ptWorldFeet[0] = srcMeta->singleView3DTracking.ptWorldFeet[0];
    dstMeta->singleView3DTracking.ptWorldFeet[1] = srcMeta->singleView3DTracking.ptWorldFeet[1];
    dstMeta->singleView3DTracking.ptImgFeet[0] = srcMeta->singleView3DTracking.ptImgFeet[0];
    dstMeta->singleView3DTracking.ptImgFeet[1] = srcMeta->singleView3DTracking.ptImgFeet[1];
    dstMeta->singleView3DTracking.convexHull.numFilled = srcMeta->singleView3DTracking.convexHull.numFilled;
    dstMeta->singleView3DTracking.convexHull.points =
        g_memdup(srcMeta->singleView3DTracking.convexHull.points,
                    srcMeta->singleView3DTracking.convexHull.numFilled * 2 * sizeof(gint));

  } else {
    dstMeta->has3DTracking = false;
  }
  return dstMeta;
}

static void meta_free_func(gpointer data, gpointer user_data) {
  NvDsUserMeta *user_meta = (NvDsUserMeta *)data;
  NvDsEventMsgMeta *srcMeta = (NvDsEventMsgMeta *)user_meta->user_meta_data;
  user_meta->user_meta_data = NULL;

  if (srcMeta->ts) {
    g_free(srcMeta->ts);
  }

  if (srcMeta->objSignature.size > 0) {
    g_free(srcMeta->objSignature.signature);
    srcMeta->objSignature.size = 0;
  }

  if (srcMeta->objectId) {
    g_free(srcMeta->objectId);
  }

  if (srcMeta->sensorStr) {
    g_free(srcMeta->sensorStr);
  }

  if (srcMeta->extMsgSize > 0) {
    if (srcMeta->objType == NVDS_OBJECT_TYPE_VEHICLE) {
      NvDsVehicleObject *obj = (NvDsVehicleObject *)srcMeta->extMsg;
      if (obj->type) g_free(obj->type);
      if (obj->color) g_free(obj->color);
      if (obj->make) g_free(obj->make);
      if (obj->model) g_free(obj->model);
      if (obj->license) g_free(obj->license);
      if (obj->region) g_free(obj->region);
    } else if (srcMeta->objType == NVDS_OBJECT_TYPE_PERSON) {
      NvDsPersonObject *obj = (NvDsPersonObject *)srcMeta->extMsg;

      if (obj->gender) g_free(obj->gender);
      if (obj->cap) g_free(obj->cap);
      if (obj->hair) g_free(obj->hair);
      if (obj->apparel) g_free(obj->apparel);
    }
    //! Extensions for Fewshot Learning
    else if (srcMeta->objType == NVDS_OBJECT_TYPE_PRODUCT) {
      NvDsProductObject *obj = (NvDsProductObject *)srcMeta->extMsg;

      if (obj->brand) g_free(obj->brand);
      if (obj->type) g_free(obj->type);
      if (obj->shape) g_free(obj->shape);
    }

    g_free(srcMeta->extMsg);
    srcMeta->extMsg = NULL;
    srcMeta->extMsgSize = 0;
  }

  if (srcMeta->embedding.embedding_vector) {
    g_free(srcMeta->embedding.embedding_vector);
  }
  srcMeta->embedding.embedding_length = 0;

  if (srcMeta->has3DTracking && srcMeta->singleView3DTracking.convexHull.points) {
    g_free(srcMeta->singleView3DTracking.convexHull.points);
    srcMeta->singleView3DTracking.convexHull.numFilled = 0;
  }

  g_free(srcMeta);
}

#ifdef GENERATE_DUMMY_META_EXT
static void generate_vehicle_meta(gpointer data) {
  NvDsVehicleObject *obj = (NvDsVehicleObject *)data;

  obj->type = g_strdup("sedan-dummy");
  obj->color = g_strdup("blue");
  obj->make = g_strdup("Bugatti");
  obj->model = g_strdup("M");
  obj->license = g_strdup("XX1234");
  obj->region = g_strdup("CA");
}

static void generate_person_meta(gpointer data) {
  NvDsPersonObject *obj = (NvDsPersonObject *)data;
  obj->age = 0;
  obj->cap = g_strdup("");
  obj->hair = g_strdup("");
  obj->gender = g_strdup("");
  obj->apparel = g_strdup("");
}
//! Extensions for Fewshot Learning
// Create product meta object
static void generate_product_meta(gpointer data) {
  NvDsProductObject *obj = (NvDsProductObject *)data;
  obj->brand = g_strdup("");
  obj->type = g_strdup("");
  obj->shape = g_strdup("");
}
#endif /**< GENERATE_DUMMY_META_EXT */

static void destroy_embedding_queue() {
  for (gint stream_id = 0; stream_id < MAX_SOURCE_BINS; stream_id++) {
    GQueue *prev_frames_embedding = testAppCtx->streams[stream_id].frame_embedding_queue;

    /** Remove outdated embedding*/
    if (prev_frames_embedding) {
      while (!g_queue_is_empty(prev_frames_embedding)) {
        FrameEmbedding *frame_embedding = (FrameEmbedding *) g_queue_pop_tail(prev_frames_embedding);
        for (GList *l = frame_embedding->obj_embeddings; l; l = l->next) {
          ObjEmbedding *obj_emb = (ObjEmbedding *) l->data;
          g_free(obj_emb->embedding);
          g_free(obj_emb);
        }
        g_list_free(frame_embedding->obj_embeddings);
        g_free(frame_embedding);
      }
      g_queue_free(prev_frames_embedding);
    }
  }
}

static void pop_embedding_queue(gint stream_id, gint frame_num) {
    GQueue *prev_frames_embedding = testAppCtx->streams[stream_id].frame_embedding_queue;

    if (prev_frames_embedding) {  /** Remove outdated embedding*/
      while (!g_queue_is_empty(prev_frames_embedding)) {
        FrameEmbedding *frame_embedding = (FrameEmbedding *) g_queue_peek_tail(prev_frames_embedding);
        if (frame_embedding->frame_num < frame_num - (gint) tracker_reid_store_age) {
          frame_embedding = (FrameEmbedding *) g_queue_pop_tail(prev_frames_embedding);
          for (GList *l = frame_embedding->obj_embeddings; l; l = l->next) {
            ObjEmbedding *obj_emb = (ObjEmbedding *) l->data;
            g_free(obj_emb->embedding);
            g_free(obj_emb);
          }
          g_list_free(frame_embedding->obj_embeddings);
          g_free(frame_embedding);
        }
        else {
          break;
        }
      }
    }
}

float *retrieve_embedding_queue(gint stream_id, gint frame_num, guint64 target_obj_id, int* p_num_elements) {
  float* embedding_data = NULL;
  GQueue *prev_frames_embedding = testAppCtx->streams[stream_id].frame_embedding_queue;

  if (prev_frames_embedding == NULL || g_queue_is_empty(prev_frames_embedding))
    return embedding_data;

  /** Find history embedding*/
  /**When queue tail is not reached and embedding not found */
  for (guint i=0; i < g_queue_get_length(prev_frames_embedding) && embedding_data == NULL; i++) {

    FrameEmbedding *frame_embedding = (FrameEmbedding *) g_queue_peek_nth(prev_frames_embedding, i);
    /** Out of history range */
    if (frame_embedding->frame_num < frame_num - (gint) tracker_reid_store_age)
      break;

    for (GList *l = frame_embedding->obj_embeddings; l; l = l->next) {
      ObjEmbedding *obj_emb = (ObjEmbedding *) l->data;
      if (obj_emb->object_id == target_obj_id) {
        embedding_data = obj_emb->embedding;
        *p_num_elements = obj_emb->num_elements;
        break;
      }
    }
  }
  return embedding_data;
}

static void push_to_embedding_queue(gint stream_id, FrameEmbedding *frame_embedding) {
  if (testAppCtx->streams[stream_id].frame_embedding_queue == NULL) {
    testAppCtx->streams[stream_id].frame_embedding_queue = g_queue_new();
  }
  g_queue_push_head (testAppCtx->streams[stream_id].frame_embedding_queue, frame_embedding);
}
void analytics_custom_parse_direction_obj_data (NvDsObjectMeta *obj_meta, AnalyticsUserMeta *data);

static void generate_event_msg_meta(AppCtx *appCtx, gpointer data,
                                    gint class_id, gboolean useTs,
                                    GstClockTime ts, gchar *src_uri,
                                    gint stream_id, guint sensor_id,
                                    NvDsObjectMeta *obj_params, float scaleW,
                                    float scaleH, NvDsFrameMeta *frame_meta,
                                    float *embedding_data, int numElements,
                                    gboolean embedding_on_device) {
  NvDsEventMsgMeta *meta = (NvDsEventMsgMeta *)data;
  GstClockTime ts_generated = 0;

  meta->objType = NVDS_OBJECT_TYPE_UNKNOWN; /**< object unknown */
  /* The sensor_id is parsed from the source group name which has the format
   * [source<sensor-id>]. */
  meta->sensorId = sensor_id;
  meta->placeId = sensor_id;
  meta->moduleId = sensor_id;
  meta->frameId = frame_meta->frame_num;
  meta->ts = (gchar *)g_malloc0(MAX_TIME_STAMP_LEN + 1);
  meta->objectId = (gchar *)g_malloc0(MAX_LABEL_SIZE);
  // embedding_data
  if (embedding_data) {
    // printf("Malloc embedding: %d\n", numElements*4);
    meta->embedding.embedding_vector =
        (float *)g_malloc0(numElements * sizeof(float));
    if (embedding_on_device) {
      cudaMemcpy(meta->embedding.embedding_vector, embedding_data,
                 numElements * sizeof(float), cudaMemcpyDeviceToHost);
    } else {
      cudaMemcpy(meta->embedding.embedding_vector, embedding_data,
                 numElements * sizeof(float), cudaMemcpyHostToHost);
    }
    meta->embedding.embedding_length = numElements;
  } else {
    meta->embedding.embedding_length = 0;
  }

  meta->has3DTracking = false;

  /*  for (NvDsMetaList *l_user = obj_params->obj_user_meta_list;
              l_user != NULL; l_user = l_user->next) {
           NvDsUserMeta *user_meta = (NvDsUserMeta *)l_user->data;

     if (user_meta->base_meta.meta_type == NVDS_OBJ_IMAGE_FOOT_LOCATION) {
       meta->has3DTracking = true;
       float *pPtFeet = (float*)user_meta->user_meta_data;
       meta->singleView3DTracking.ptImgFeet[0] = pPtFeet[0];
       meta->singleView3DTracking.ptImgFeet[1] = pPtFeet[1];
     }
     else if (user_meta->base_meta.meta_type == NVDS_OBJ_WORLD_FOOT_LOCATION) {
       float *pPtFeet = (float*)user_meta->user_meta_data;
       meta->singleView3DTracking.ptWorldFeet[0] = pPtFeet[0];
       meta->singleView3DTracking.ptWorldFeet[1] = pPtFeet[1];
     }
     else if (user_meta->base_meta.meta_type == NVDS_OBJ_VISIBILITY) {
       meta->singleView3DTracking.visibility =
   *(float*)user_meta->user_meta_data;
     }
     else if (user_meta->base_meta.meta_type == NVDS_OBJ_IMAGE_CONVEX_HULL) {
       NvDsObjConvexHull* pConvexHull = (NvDsObjConvexHull *)
   user_meta->user_meta_data; meta->singleView3DTracking.convexHull.points =
   g_malloc0(sizeof(gint) * pConvexHull->numPointsAllocated * 2);
       meta->singleView3DTracking.convexHull.numFilled = pConvexHull->numPoints;
       for (uint32_t i=0; i < pConvexHull->numPoints; i++) {
         meta->singleView3DTracking.convexHull.points[2*i] =
   pConvexHull->list[2*i]; meta->singleView3DTracking.convexHull.points[2*i+1] =
   pConvexHull->list[2*i+1];
       }
     }
   }*/

  meta->confidence = obj_params->confidence;

  strncpy(meta->objectId, obj_params->obj_label, MAX_LABEL_SIZE);

  /** INFO: This API is called once for every 30 frames (now) */
  if (useTs && src_uri) {
    ts_generated = generate_ts_rfc3339_from_ts(meta->ts, MAX_TIME_STAMP_LEN, ts,
                                               src_uri, stream_id);
  } else {
    generate_ts_rfc3339(meta->ts, MAX_TIME_STAMP_LEN);
  }

  /**
   * Valid attributes in the metadata sent over nvmsgbroker:
   * a) Sensor ID (shall be configured in nvmsgconv config file)
   * b) bbox info (meta->bbox) <- obj_params->rect_params (attr_info have sgie
   * info) c) tracking ID (meta->trackingId) <- obj_params->object_id
   */

  /** bbox - resolution is scaled by nvinfer back to
   * the resolution provided by streammux
   * We have to scale it back to original stream resolution
   */

  meta->bbox.left = obj_params->rect_params.left * scaleW;
  meta->bbox.top = obj_params->rect_params.top * scaleH;
  meta->bbox.width = obj_params->rect_params.width * scaleW;
  meta->bbox.height = obj_params->rect_params.height * scaleH;

  /** tracking ID */
  meta->trackingId = obj_params->object_id;

  /** sensor ID when streams are added using nvmultiurisrcbin REST API */
  NvDsSensorInfo *sensorInfo = get_sensor_info(appCtx, stream_id);
  if (sensorInfo) {
    /** this stream was added using REST API; we have Sensor Info!
     * Note: using NvDsSensorInfo->sensor_name instead of sensor_id
     * to use camera_name field from the stream/add REST API
     */
    // g_print(
    //     "this stream [%d:%s] was added using REST API; we have Sensor
    //     Info\n", sensorInfo->source_id, sensorInfo->sensor_id);
    meta->sensorStr = g_strdup(sensorInfo->sensor_name);
  }
  //g_print("sensorId:%d sensorStr:%s \n",meta->sensorId, meta->sensorStr);
  (void)ts_generated;

  /*
   * This demonstrates how to attach custom objects.
   * Any custom object as per requirement can be generated and attached
   * like NvDsVehicleObject / NvDsPersonObject. Then that object should
   * be handled in gst-nvmsgconv component accordingly.
   */
  meta->objType = NVDS_OBJECT_TYPE_PERSON;
  NvDsPersonObject *obj =
      (NvDsPersonObject *)g_malloc0(sizeof(NvDsPersonObject));
  generate_person_meta(obj);

  meta->extMsg = obj;
  meta->extMsgSize = sizeof(NvDsPersonObject);

    gchar *image_path = NULL;
  for (NvDsUserMetaList *l_user = obj_params->obj_user_meta_list; l_user != NULL; l_user = l_user->next) {
        NvDsUserMeta *user_meta = (NvDsUserMeta *)l_user->data;
        if (user_meta->base_meta.meta_type == NVDS_CUSTOM_IMAGE_PATH_META) {
          NvDsImagePathMeta *path_meta = (NvDsImagePathMeta *)user_meta->user_meta_data;
          image_path = path_meta->image_path;
          //g_print("ImgPATH %s: ", image_path);
          break; 
        }
      }
  if (image_path) obj->hair = g_strdup(image_path);

  if (!appCtx->config.dsanalytics_config.enable) {
    g_print("Unable to get nvdsanalytics src pad\n");
    return;
  }
  AnalyticsUserMeta *user_data =
      (AnalyticsUserMeta *)g_malloc0(sizeof(AnalyticsUserMeta));
  analytics_custom_parse_direction_obj_data(obj_params, user_data);
  if (user_data->direction != NULL) {

  obj->gender = g_strdup(user_data->direction);
  obj->cap = g_strdup("Crossed");

    // meta->extMsgSize = sizeof(AnalyticsUserMeta);
  } 
  // if (terminated_id) {

  //   meta->type = NVDS_EVENT_STOPPED;
  //   meta->objType = NVDS_OBJECT_TYPE_PERSON;
  //   NvDsPersonObject *obj =
  //       (NvDsPersonObject *)g_malloc0(sizeof(NvDsPersonObject));
  //   generate_person_meta(obj);
  //   obj->cap = g_strdup("Terminated");
  //   meta->extMsg = obj;
  //   meta->extMsgSize = sizeof(NvDsPersonObject);
  // } 
}
void after_pgie_image_meta_save(AppCtx *appCtx, GstBuffer *buf,
                                NvDsBatchMeta *batch_meta, guint index,
                                ImageMetaConsumerWrapper *consumer);
/**
 * Callback function to be called once all inferences (Primary + Secondary)
 * are done. This is opportunity to modify content of the metadata.
 * e.g. Here Person is being replaced with Man/Woman and corresponding counts
 * are being maintained. It should be modified according to network classes
 * or can be removed altogether if not required.
 */

static void bbox_generated_probe_after_analytics(AppCtx *appCtx, GstBuffer *buf,
                                                 NvDsBatchMeta *batch_meta,
                                                 guint index) {


  NvDsObjectMeta *obj_meta = NULL;
  GstClockTime buffer_pts = 0;
  guint32 stream_id = 0;
  gboolean isTerminatedTrack = FALSE;
  
  after_pgie_image_meta_save(appCtx, buf, batch_meta, index,
                             g_img_meta_consumer);
  /** Find batch reid tensor in batch user meta. */
  // NvDsReidTensorBatch *pReidTensor = NULL;
  NvDsObjReid *pReidObj = NULL;
  NvDsTargetMiscDataBatch *pTrackerObj = NULL;
  if (use_tracker_reid) {
    for (NvDsUserMetaList *l_batch_user = batch_meta->batch_user_meta_list;
         l_batch_user != NULL; l_batch_user = l_batch_user->next) {
      NvDsUserMeta *user_meta = (NvDsUserMeta *)l_batch_user->data;
      if (user_meta &&
          user_meta->base_meta.meta_type == NVDS_TRACKER_BATCH_REID_META) {
        // pReidTensor = (NvDsReidTensorBatch *) (user_meta->user_meta_data);
        pReidObj = (NvDsObjReid *)(user_meta->user_meta_data);
      }
      if (user_meta &&
          user_meta->base_meta.meta_type == NVDS_TRACKER_TERMINATED_LIST_META) {
        pTrackerObj = (NvDsTargetMiscDataBatch *)(user_meta->user_meta_data);
        if (pTrackerObj != NULL) {
          for (uint32_t i = 0; i < pTrackerObj->numFilled; ++i) {
            NvDsTargetMiscDataStream *stream = &pTrackerObj->list[i];
            
            for (uint32_t j = 0; j < stream->numFilled; ++j) {
              NvDsTargetMiscDataObject *obj = &stream->list[j];
              g_print("StreamID %u: Terminated Unique ID: %lu\n", stream->streamID, obj->uniqueId);
            }
            }
          }
      }
    }
  }

  for (NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame != NULL;
       l_frame = l_frame->next) {
    NvDsFrameMeta *frame_meta = (NvDsFrameMeta *)l_frame->data;
    stream_id = frame_meta->source_id;
    if (testAppCtx->streams[stream_id].meta_number == 0) {
      gchar timestamp[MAX_TIME_STAMP_LEN] = {0};
      guint32 stream_id = frame_meta->source_id;
      GstClockTime ts = frame_meta->buf_pts;
      generate_ts_rfc3339_from_ts(
          timestamp, MAX_TIME_STAMP_LEN, ts,
          appCtx->config.multi_source_config[stream_id].uri, stream_id);
      testAppCtx->streams[stream_id].meta_number++;
    }

    //! DEBUGGER_START for ending at 5mins
    if (log_level >= 100) {
      if (frame_meta->frame_num >= 9000) {
        quit = TRUE;
        g_main_loop_quit(main_loop);
        break;
      }
    }
    //! DEBUGGER_END

    GstClockTime buf_ntp_time = 0;
    if (playback_utc == FALSE) {
      /** Calculate the buffer-NTP-time
       * derived from this stream's RTCP Sender Report here:
       */
      StreamSourceInfo *src_stream = &testAppCtx->streams[stream_id];
      buf_ntp_time = frame_meta->ntp_timestamp;

      if (buf_ntp_time < src_stream->last_ntp_time) {
        NVGSTDS_WARN_MSG_V("Source %d: NTP timestamps are backward in time."
                           " Current: %lu previous: %lu",
                           stream_id, buf_ntp_time, src_stream->last_ntp_time);
      }
      src_stream->last_ntp_time = buf_ntp_time;
    }

    FrameEmbedding *frame_embedding = NULL;
    if (use_tracker_reid && tracker_reid_store_age > 0) {
      pop_embedding_queue(stream_id, frame_meta->frame_num);
    }

    GList *l;

    for (l = frame_meta->obj_meta_list; l != NULL; l = l->next) {
      /* Now using above information we need to form a text that should
       * be displayed on top of the bounding box, so lets form it here. */

      obj_meta = (NvDsObjectMeta *)(l->data);
      if (model_used == APP_CONFIG_ANALYTICS_MTMC &&
          obj_meta->class_id != target_class) {
        continue;
      }
      if (!(frame_meta->frame_num % message_rate)) {
        /**
         * Enable only if this callback is after tiler
         * NOTE: Scaling back code-commented
         * now that bbox_generated_probe_after_analytics() is post analytics
         * (say pgie, tracker or sgie)
         * and before tiler, no plugin shall scale metadata and will be
         * corresponding to the nvstreammux resolution
         */
        float scaleW = 0;
        float scaleH = 0;
        /* Frequency of messages to be send will be based on use case.
         * Here message is being sent for first object every 30 frames.
         */
        buffer_pts = frame_meta->buf_pts;
        if (!appCtx->config.streammux_config.pipeline_width ||
            !appCtx->config.streammux_config.pipeline_height) {
          if (log_level >= LOG_LVL_ERROR) {
            g_print("invalid pipeline params\n");
          }
          return;
        }
        if (log_level >= LOG_LVL_DEBUG) {
          LOGD("stream %d==%d [%d X %d]\n", frame_meta->source_id,
               frame_meta->pad_index, frame_meta->source_frame_width,
               frame_meta->source_frame_height);
        }
        scaleW = (float)frame_meta->source_frame_width /
                 appCtx->config.streammux_config.pipeline_width;
        scaleH = (float)frame_meta->source_frame_height /
                 appCtx->config.streammux_config.pipeline_height;

        if (log_level == 99 || log_level == 100) {
          if (playback_utc == FALSE) {
            g_print("[DEBUG]: Timestamp: Frame(frame_meta->buf_pts) "
                    "[%" GST_TIME_FORMAT "] RTCP "
                    "sender report(buf_ntp_time) [%" GST_TIME_FORMAT
                    " ]; Playback (False)\n",
                    GST_TIME_ARGS(frame_meta->buf_pts),
                    GST_TIME_ARGS(buf_ntp_time));
          } else {
            g_print("[DEBUG]: Timestamp: Frame(frame_meta->buf_pts) "
                    "[%" GST_TIME_FORMAT "] RTCP "
                    "sender report(buf_ntp_time) [%" GST_TIME_FORMAT
                    " ]; Playback (True)\n",
                    GST_TIME_ARGS(frame_meta->buf_pts),
                    GST_TIME_ARGS(buf_ntp_time));
          }
        }
        if (playback_utc == FALSE) {
          /** Use the buffer-NTP-time derived from this stream's RTCP Sender
           * Report here:
           */
          buffer_pts = buf_ntp_time;
        }

        float *embedding_data = NULL;
        int numElements = 0;
        gboolean embedding_on_device = false;

        //! Attaching Embedding tensor metadata
        for (NvDsMetaList *l_user = obj_meta->obj_user_meta_list;
             l_user != NULL; l_user = l_user->next) {
          NvDsUserMeta *user_meta = (NvDsUserMeta *)l_user->data;
          if (use_tracker_reid && user_meta->base_meta.meta_type == NVDS_TRACKER_OBJ_REID_META) {
            /** Use embedding from tracker reid*/
            // gint reidInd = *((int32_t *) (user_meta->user_meta_data));
            // NvDsObjReid *pReidObj = (NvDsObjReid *) (user_meta->user_meta_data);

            if (pReidObj != NULL && pReidObj->ptr_host != NULL && pReidObj->featureSize > 0) {
              numElements = pReidObj->featureSize;
              embedding_data = (float *)(pReidObj->ptr_host);

              if (tracker_reid_store_age > 0) {
                if (frame_embedding == NULL) {
                  frame_embedding = (FrameEmbedding *) g_malloc0(sizeof(FrameEmbedding));
                  frame_embedding->frame_num = frame_meta->frame_num;
                  frame_embedding->obj_embeddings = NULL;
                }
                ObjEmbedding *obj_emb = (ObjEmbedding *) g_malloc0(sizeof(ObjEmbedding));
                obj_emb->object_id = obj_meta->object_id;
                obj_emb->num_elements = numElements;
                obj_emb->embedding = g_malloc0(sizeof(float) * numElements);
                // memcpy(obj_emb->embedding, embedding_data, sizeof(float) * numElements);
                cudaMemcpy(obj_emb->embedding, (float *)(pReidObj->ptr_host),
                  sizeof(float) * numElements, cudaMemcpyDeviceToHost);
                frame_embedding->obj_embeddings = g_list_append(frame_embedding->obj_embeddings, obj_emb);
              }
            }
          }
          else if ((!use_tracker_reid) && user_meta->base_meta.meta_type == NVDSINFER_TENSOR_OUTPUT_META) {
            /* Use embedding from SGIE reid */
            NvDsInferTensorMeta *tensor_meta =
                (NvDsInferTensorMeta *)user_meta->user_meta_data;

            NvDsInferDims embedding_dims =
                tensor_meta->output_layers_info[0].inferDims;

            numElements = embedding_dims.d[0];
            embedding_data = (float *)(tensor_meta->out_buf_ptrs_dev[0]);
            embedding_on_device = true;
          }
        }

        if (use_tracker_reid && tracker_reid_store_age > 0 && embedding_data == NULL) {
          embedding_data = retrieve_embedding_queue(stream_id, frame_meta->frame_num,
            obj_meta->object_id, &numElements);
        }

        /** Generate NvDsEventMsgMeta for every object */
        NvDsEventMsgMeta *msg_meta = (NvDsEventMsgMeta *) g_malloc0(sizeof(NvDsEventMsgMeta));
        generate_event_msg_meta(
            appCtx, msg_meta, obj_meta->class_id, TRUE,
            /**< useTs NOTE: Pass FALSE for files without base-timestamp in URI
             */
            buffer_pts, appCtx->config.multi_source_config[stream_id].uri,
            stream_id, stream_id, obj_meta, scaleW, scaleH, frame_meta,
            embedding_data, numElements, embedding_on_device);
        if (log_level == 99 || log_level == 100) {
          g_print("[DEBUG]: Timestamp after msg meta creation: %s\n",
                  msg_meta->ts);

          g_print(
              "[DEBUG]: NvDsEventMsgMeta: {'sensor-id': '%d', 'frameId': '%d', "
              "'timestamp': '%s', 'object-id': '%s', 'confidence': '%f', "
              "'bbox': [%.2f, %.2f, %.2f, %.2f]}\n",
              msg_meta->sensorId, msg_meta->frameId, msg_meta->ts,
              msg_meta->objectId, msg_meta->confidence, msg_meta->bbox.left,
              msg_meta->bbox.top, msg_meta->bbox.width, msg_meta->bbox.height);
        }

        testAppCtx->streams[stream_id].meta_number++;
        NvDsUserMeta *user_event_meta =
            nvds_acquire_user_meta_from_pool(batch_meta);
        if (user_event_meta) {
          /*
           * Since generated event metadata has custom objects for
           * Vehicle / Person which are allocated dynamically, we are
           * setting copy and free function to handle those fields when
           * metadata copy happens between two components.
           */
          user_event_meta->user_meta_data = (void *)msg_meta;
          user_event_meta->base_meta.batch_meta = batch_meta;
          user_event_meta->base_meta.meta_type = NVDS_EVENT_MSG_META;
          user_event_meta->base_meta.copy_func =
              (NvDsMetaCopyFunc)meta_copy_func;
          user_event_meta->base_meta.release_func =
              (NvDsMetaReleaseFunc)meta_free_func;
          nvds_add_user_meta_to_frame(frame_meta, user_event_meta);
        } else {
          if (log_level >= LOG_LVL_ERROR) {
            g_print("Error in attaching event meta to buffer\n");
          }
        }
      }
    }

    if (use_tracker_reid && tracker_reid_store_age > 0 && frame_embedding) {
      push_to_embedding_queue(stream_id, frame_embedding);
    }

    //! DEBUGGER_START to add timestamp to OSD label
    if (log_level == 100 || log_level == 99) {
      GstClockTime ts_generated;
      NvDsDisplayMeta *display_meta =
          nvds_acquire_display_meta_from_pool(batch_meta);
      NvOSD_TextParams *txt_params = &display_meta->text_params[0];
      display_meta->num_labels = 1;
      txt_params->display_text = g_malloc0(MAX_DISPLAY_LEN);
      char timestamp_buf[MAX_TIME_STAMP_LEN];
      ts_generated = generate_ts_rfc3339_from_ts(
          timestamp_buf, MAX_TIME_STAMP_LEN, buf_ntp_time, "", stream_id);
      int offset = snprintf(txt_params->display_text, MAX_DISPLAY_LEN, "%s",
                            timestamp_buf);
      /* Now set the offsets where the string should appear */
      txt_params->x_offset = 10;
      txt_params->y_offset = 12;

      /* Font , font-color and font-size */
      txt_params->font_params.font_name = "Serif";
      txt_params->font_params.font_size = 10;
      txt_params->font_params.font_color.red = 1.0;
      txt_params->font_params.font_color.green = 1.0;
      txt_params->font_params.font_color.blue = 1.0;
      txt_params->font_params.font_color.alpha = 1.0;

      /* Text background color */
      txt_params->set_bg_clr = 1;
      txt_params->text_bg_clr.red = 0.0;
      txt_params->text_bg_clr.green = 0.0;
      txt_params->text_bg_clr.blue = 0.0;
      txt_params->text_bg_clr.alpha = 1.0;
      nvds_add_display_meta_to_frame(frame_meta, display_meta);
    }

    //! DEBUGGER_END

    testAppCtx->streams[stream_id].frameCount++;
  }
}

/** TransferLearning */



/** @{ imported from deepstream-app as is */

/**
 * Function to handle program interrupt signal.
 * It installs default handler after handling the interrupt.
 */
static void _intr_handler(int signum) {
  struct sigaction action;

  NVGSTDS_ERR_MSG_V("User Interrupted.. \n");

  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_DFL;

  sigaction(SIGINT, &action, NULL);

  cintr = TRUE;
}

/**
 * callback function to print the performance numbers of each stream.
 */
static void perf_cb(gpointer context, NvDsAppPerfStruct *str) {
  static guint header_print_cnt = 0;
  guint i;
  AppCtx *appCtx = (AppCtx *)context;
  guint numf = str->num_instances;

  g_mutex_lock(&fps_lock);
  guint active_src_count = 0;

  if (!str->use_nvmultiurisrcbin) {
    for (i = 0; i < numf; i++) {
      fps[i] = str->fps[i];
      if (fps[i]) {
        active_src_count++;
      }
      fps_avg[i] = str->fps_avg[i];
    }
    g_print("Active sources : %u\n", active_src_count);
    if (header_print_cnt % 20 == 0) {
      g_print("\n**PERF:  ");
      for (i = 0; i < numf; i++) {
        g_print("FPS %d (Avg)\t", i);
      }
      g_print("\n");
      header_print_cnt = 0;
    }
    header_print_cnt++;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    printf("%s", asctime(tm));
    if (num_instances > 1)
      g_print("PERF(%d): ", appCtx->index);
    else
      g_print("**PERF:  ");

    for (i = 0; i < numf; i++) {
      g_print("%.2f (%.2f)\t", fps[i], fps_avg[i]);
    }
  } else {
    for (guint j = 0; j < str->active_source_size; j++) {
      i = str->source_detail[j].source_id;
      fps[i] = str->fps[i];
      if (fps[i]) {
        active_src_count++;
      }
      fps_avg[i] = str->fps_avg[i];
    }
    g_print("Active sources : %u\n", active_src_count);
    if (header_print_cnt % 20 == 0) {
      g_print("\n**PERF:  ");
      for (guint j = 0; j < str->active_source_size; j++) {
        i = str->source_detail[j].source_id;
        g_print("FPS %d (Avg)\t", i);
      }
      g_print("\n");
      header_print_cnt = 0;
    }
    header_print_cnt++;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    printf("%s", asctime(tm));
    if (num_instances > 1)
      g_print("PERF(%d): ", appCtx->index);
    else
      g_print("**PERF:  ");

    g_print("\n");
    for (guint j = 0; j < str->active_source_size; j++) {
      i = str->source_detail[j].source_id;
      g_print("%.2f (%.2f)\t", fps[i], fps_avg[i]);
      if (str->stream_name_display)
        g_print("source_id : %d stream_name %s \n", i,
                str->source_detail[j].stream_name);
    }
  }
  g_print("\n");
  g_mutex_unlock(&fps_lock);
}

/**
 * Loop function to check the status of interrupts.
 * It comes out of loop if application got interrupted.
 */
static gboolean check_for_interrupt(gpointer data) {
  if (quit) {
    return FALSE;
  }

  if (cintr) {
    cintr = FALSE;

    quit = TRUE;
    g_main_loop_quit(main_loop);

    return FALSE;
  }
  return TRUE;
}

/*
 * Function to install custom handler for program interrupt signal.
 */
static void _intr_setup(void) {
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = _intr_handler;

  sigaction(SIGINT, &action, NULL);
}

static gboolean kbhit(void) {
  struct timeval tv;
  fd_set rdfs;

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  FD_ZERO(&rdfs);
  FD_SET(STDIN_FILENO, &rdfs);

  select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
  return FD_ISSET(STDIN_FILENO, &rdfs);
}

/*
 * Function to enable / disable the canonical mode of terminal.
 * In non canonical mode input is available immediately (without the user
 * having to type a line-delimiter character).
 */
static void changemode(int dir) {
  static struct termios oldt, newt;

  if (dir == 1) {
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  } else
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

static void print_runtime_commands(void) {
  g_print("\nRuntime commands:\n"
          "\th: Print this help\n"
          "\tq: Quit\n\n"
          "\tp: Pause\n"
          "\tr: Resume\n\n");

  if (appCtx[0]->config.tiled_display_config.enable) {
    g_print(
        "NOTE: To expand a source in the 2D tiled display and view object "
        "details,"
        " left-click on the source.\n"
        "      To go back to the tiled display, right-click anywhere on the "
        "window.\n\n");
  }
}

/**
 * Loop function to check keyboard inputs and status of each pipeline.
 */
static gboolean event_thread_func(gpointer arg) {
  guint i;
  gboolean ret = TRUE;

  // Check if all instances have quit
  for (i = 0; i < num_instances; i++) {
    if (!appCtx[i]->quit)
      break;
  }

  if (i == num_instances) {
    quit = TRUE;
    g_main_loop_quit(main_loop);
    return FALSE;
  }
  // Check for keyboard input
  if (!kbhit()) {
    // continue;
    return TRUE;
  }
  int c = fgetc(stdin);

  gint source_id;
  GstElement *tiler = appCtx[rcfg]->pipeline.tiled_display_bin.tiler;

  if (appCtx[rcfg]->config.tiled_display_config.enable) {
    g_object_get(G_OBJECT(tiler), "show-source", &source_id, NULL);

    if (selecting) {
      if (rrowsel == FALSE) {
        if (c >= '0' && c <= '9') {
          rrow = c - '0';
          if (log_level >= LOG_LVL_DEBUG) {
            g_print("--selecting source  row %d--\n", rrow);
          }
          rrowsel = TRUE;
        }
      } else {
        if (c >= '0' && c <= '9') {
          int tile_num_columns =
              appCtx[rcfg]->config.tiled_display_config.columns;
          rcol = c - '0';
          selecting = FALSE;
          rrowsel = FALSE;
          source_id = tile_num_columns * rrow + rcol;
          if (log_level >= LOG_LVL_DEBUG) {
            g_print("--selecting source  col %d sou=%d--\n", rcol, source_id);
          }
          if (source_id >= (gint)appCtx[rcfg]->config.num_source_sub_bins) {
            source_id = -1;
          } else {
            appCtx[rcfg]->show_bbox_text = TRUE;
            appCtx[rcfg]->active_source_index = source_id;
            g_object_set(G_OBJECT(tiler), "show-source", source_id, NULL);
          }
        }
      }
    }
  }
  switch (c) {
  case 'h':
    print_runtime_commands();
    break;
  case 'p':
    for (i = 0; i < num_instances; i++)
      pause_pipeline(appCtx[i]);
    break;
  case 'r':
    for (i = 0; i < num_instances; i++)
      resume_pipeline(appCtx[i]);
    break;
  case 'q':
    quit = TRUE;
    g_main_loop_quit(main_loop);
    ret = FALSE;
    break;
  case 'c':
    if (appCtx[rcfg]->config.tiled_display_config.enable &&
        selecting == FALSE && source_id == -1) {
      if (log_level >= LOG_LVL_DEBUG)
        g_print("--selecting config file --\n");
      c = fgetc(stdin);
      if (c >= '0' && c <= '9') {
        rcfg = c - '0';
        if (rcfg < num_instances) {
          if (log_level >= LOG_LVL_DEBUG)
            g_print("--selecting config  %d--\n", rcfg);
        } else {
          if (log_level >= LOG_LVL_DEBUG)
            g_print("--selected config file %d out of bound, reenter\n", rcfg);
          rcfg = 0;
        }
      }
    }
    break;
  case 'z':
    if (appCtx[rcfg]->config.tiled_display_config.enable && source_id == -1 &&
        selecting == FALSE) {
      if (log_level >= LOG_LVL_DEBUG)
        g_print("--selecting source --\n");
      selecting = TRUE;
    } else {
      if (!show_bbox_text) {
        GstElement *nvosd =
            appCtx[rcfg]->pipeline.instance_bins[0].osd_bin.nvosd;
        g_object_set(G_OBJECT(nvosd), "display-text", FALSE, NULL);
        g_object_set(G_OBJECT(tiler), "show-source", -1, NULL);
      }
      appCtx[rcfg]->active_source_index = -1;
      selecting = FALSE;
      rcfg = 0;
      if (log_level >= LOG_LVL_DEBUG)
        g_print("--tiled mode --\n");
    }
    break;
  default:
    break;
  }
  return ret;
}

static int get_source_id_from_coordinates(float x_rel, float y_rel,
                                          AppCtx *appCtx) {
  int tile_num_rows = appCtx->config.tiled_display_config.rows;
  int tile_num_columns = appCtx->config.tiled_display_config.columns;

  int source_id = (int)(x_rel * tile_num_columns);
  source_id += ((int)(y_rel * tile_num_rows)) * tile_num_columns;

  /* Don't allow clicks on empty tiles. */
  if (source_id >= (gint)appCtx->config.num_source_sub_bins) source_id = -1;

  return source_id;
}

/**
 * Thread to monitor X window events.
 */
static gpointer nvds_x_event_thread(gpointer data) {
  g_mutex_lock(&disp_lock);
  while (display) {
    XEvent e;
    guint index;
    while (XPending(display)) {
      XNextEvent(display, &e);
      switch (e.type) {
      case ButtonPress: {
        XWindowAttributes win_attr;
        XButtonEvent ev = e.xbutton;
        gint source_id;
        GstElement *tiler;

        XGetWindowAttributes(display, ev.window, &win_attr);

        for (index = 0; index < MAX_INSTANCES; index++)
          if (ev.window == windows[index])
            break;

        tiler = appCtx[index]->pipeline.tiled_display_bin.tiler;
        g_object_get(G_OBJECT(tiler), "show-source", &source_id, NULL);

        if (ev.button == Button1 && source_id == -1) {
          source_id = get_source_id_from_coordinates(
              ev.x * 1.0 / win_attr.width, ev.y * 1.0 / win_attr.height,
              appCtx[index]);
          if (source_id > -1) {
            g_object_set(G_OBJECT(tiler), "show-source", source_id, NULL);
            appCtx[index]->active_source_index = source_id;
            appCtx[index]->show_bbox_text = TRUE;
            GstElement *nvosd =
                appCtx[index]->pipeline.instance_bins[0].osd_bin.nvosd;
            g_object_set(G_OBJECT(nvosd), "display-text", TRUE, NULL);
          }
        } else if (ev.button == Button3) {
          g_object_set(G_OBJECT(tiler), "show-source", -1, NULL);
          appCtx[index]->active_source_index = -1;
          if (!show_bbox_text) {
            appCtx[index]->show_bbox_text = FALSE;
            GstElement *nvosd =
                appCtx[index]->pipeline.instance_bins[0].osd_bin.nvosd;
            g_object_set(G_OBJECT(nvosd), "display-text", FALSE, NULL);
          }
        }
      } break;
      case KeyRelease: {
        KeySym p, r, q;
        guint i;
        p = XKeysymToKeycode(display, XK_P);
        r = XKeysymToKeycode(display, XK_R);
        q = XKeysymToKeycode(display, XK_Q);
        if (e.xkey.keycode == p) {
          for (i = 0; i < num_instances; i++)
            pause_pipeline(appCtx[i]);
          break;
        }
        if (e.xkey.keycode == r) {
          for (i = 0; i < num_instances; i++)
            resume_pipeline(appCtx[i]);
          break;
        }
        if (e.xkey.keycode == q) {
          quit = TRUE;
          g_main_loop_quit(main_loop);
        }
      } break;
      case ClientMessage: {
        Atom wm_delete;
        for (index = 0; index < MAX_INSTANCES; index++)
          if (e.xclient.window == windows[index])
            break;

        wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", 1);
        if (wm_delete != None && wm_delete == (Atom)e.xclient.data.l[0]) {
          quit = TRUE;
          g_main_loop_quit(main_loop);
        }
      } break;
      }
    }
    g_mutex_unlock(&disp_lock);
    g_usleep(G_USEC_PER_SEC / 20);
    g_mutex_lock(&disp_lock);
  }
  g_mutex_unlock(&disp_lock);
  return NULL;
}

/**
 * callback function to add application specific metadata.
 * Here it demonstrates how to display the URI of source in addition to
 * the text generated after inference.
 */
static gboolean overlay_graphics(AppCtx *appCtx, GstBuffer *buf,
                                 NvDsBatchMeta *batch_meta, guint index) {
  return TRUE;
}

/**
 * Callback function to notify the status of the model update
 */
static void infer_model_updated_cb(GstElement *gie, gint err,
                                   const gchar *config_file) {
  double otaTime = 0;
  gettimeofday(&ota_completion_time, NULL);

  otaTime = (ota_completion_time.tv_sec - ota_request_time.tv_sec) * 1000.0;
  otaTime += (ota_completion_time.tv_usec - ota_request_time.tv_usec) / 1000.0;

  const char *err_str = (err == 0 ? "ok" : "failed");
  if (log_level >= LOG_LVL_DEBUG)
    g_print(
        "\nModel Update Status: Updated model : %s, OTATime = %f ms, result: "
        "%s "
        "\n\n",
        config_file, otaTime, err_str);
}

/**
 * Function to print detected Inotify handler events
 * Used only for debugging purposes
 */
static void display_inotify_event(struct inotify_event *i_event) {
  if (log_level >= LOG_LVL_DEBUG) {
    printf("    watch decriptor =%2d; ", i_event->wd);
    if (i_event->cookie > 0) printf("cookie =%4d; ", i_event->cookie);

    printf("mask = ");
    if (i_event->mask & IN_ACCESS) printf("IN_ACCESS ");
    if (i_event->mask & IN_ATTRIB) printf("IN_ATTRIB ");
    if (i_event->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE ");
    if (i_event->mask & IN_CLOSE_WRITE) printf("IN_CLOSE_WRITE ");
    if (i_event->mask & IN_CREATE) printf("IN_CREATE ");
    if (i_event->mask & IN_DELETE) printf("IN_DELETE ");
    if (i_event->mask & IN_DELETE_SELF) printf("IN_DELETE_SELF ");
    if (i_event->mask & IN_IGNORED) printf("IN_IGNORED ");
    if (i_event->mask & IN_ISDIR) printf("IN_ISDIR ");
    if (i_event->mask & IN_MODIFY) printf("IN_MODIFY ");
    if (i_event->mask & IN_MOVE_SELF) printf("IN_MOVE_SELF ");
    if (i_event->mask & IN_MOVED_FROM) printf("IN_MOVED_FROM ");
    if (i_event->mask & IN_MOVED_TO) printf("IN_MOVED_TO ");
    if (i_event->mask & IN_OPEN) printf("IN_OPEN ");
    if (i_event->mask & IN_Q_OVERFLOW) printf("IN_Q_OVERFLOW ");
    if (i_event->mask & IN_UNMOUNT) printf("IN_UNMOUNT ");

    if (i_event->mask & IN_CLOSE) printf("IN_CLOSE ");
    if (i_event->mask & IN_MOVE) printf("IN_MOVE ");
    if (i_event->mask & IN_UNMOUNT) printf("IN_UNMOUNT ");
    if (i_event->mask & IN_IGNORED) printf("IN_IGNORED ");
    if (i_event->mask & IN_Q_OVERFLOW) printf("IN_Q_OVERFLOW ");
    printf("\n");

    if (i_event->len > 0)
      printf("        name = %s mask= %x \n", i_event->name, i_event->mask);
  }
}

/**
 * Perform model-update OTA operation
 */
void apply_ota(AppCtx *ota_appCtx) {
  GstElement *primary_gie = NULL;

  if (ota_appCtx->override_config.primary_gie_config.enable) {
    primary_gie =
        ota_appCtx->pipeline.common_elements.primary_gie_bin.primary_gie;
    gchar *model_engine_file_path =
        ota_appCtx->override_config.primary_gie_config.model_engine_file_path;

    gettimeofday(&ota_request_time, NULL);
    if (model_engine_file_path) {
      if (log_level >= LOG_LVL_DEBUG) {
        g_print("\nNew Model Update Request %s ----> %s\n",
                GST_ELEMENT_NAME(primary_gie), model_engine_file_path);
      }
      g_object_set(G_OBJECT(primary_gie), "model-engine-file",
                   model_engine_file_path, NULL);
    } else {
      if (log_level >= LOG_LVL_DEBUG) {
        g_print("\nInvalid New Model Update Request received. Property "
                "model-engine-path is not set\n");
      }
    }
  }
}

/**
 * Independent thread to perform model-update OTA process based on the inotify
 * events It handles currently two scenarios 1) Local Model Update Request (e.g.
 * Standalone Appliation) In this case, notifier handler watches for the
 * ota_override_file changes 2) Cloud Model Update Request (e.g. EGX with
 * Kubernetes) In this case, notifier handler watches for the ota_override_file
 * changes along with
 *    ..data directory which gets mounted by EGX deployment in Kubernetes
 * environment.
 */
gpointer ota_handler_thread(gpointer data) {
  int length, i = 0;
  char buffer[INOTIFY_EVENT_BUF_LEN];
  OTAInfo *ota = (OTAInfo *)data;
  gchar *ota_ds_config_file = ota->override_cfg_file;
  AppCtx *ota_appCtx = ota->appCtx;
  struct stat file_stat = {0};
  GstElement *primary_gie = NULL;
  gboolean connect_pgie_signal = FALSE;

  ota_appCtx->ota_inotify_fd = inotify_init();

  if (ota_appCtx->ota_inotify_fd < 0) {
    perror("inotify_init");
    return NULL;
  }

  char *real_path_ds_config_file = realpath(ota_ds_config_file, NULL);
  if (log_level >= LOG_LVL_DEBUG) {
    g_print("REAL PATH = %s\n", real_path_ds_config_file);
  }

  gchar *ota_dir = g_path_get_dirname(real_path_ds_config_file);
  ota_appCtx->ota_watch_desc =
      inotify_add_watch(ota_appCtx->ota_inotify_fd, ota_dir, IN_ALL_EVENTS);

  int ret = lstat(ota_ds_config_file, &file_stat);
  ret = ret;

  if (S_ISLNK(file_stat.st_mode)) {
    if (log_level >= LOG_LVL_DEBUG) {
      printf(" Override File Provided is Soft Link\n");
    }
    gchar *parent_ota_dir = g_strdup_printf("%s/..", ota_dir);
    ota_appCtx->ota_watch_desc = inotify_add_watch(
        ota_appCtx->ota_inotify_fd, parent_ota_dir, IN_ALL_EVENTS);
  }

  while (1) {
    i = 0;
    length = read(ota_appCtx->ota_inotify_fd, buffer, INOTIFY_EVENT_BUF_LEN);

    if (length < 0) {
      perror("read");
    }

    if (quit == TRUE) goto done;

    while (i < length) {
      struct inotify_event *event = (struct inotify_event *)&buffer[i];

      // Enable below function to print the inotify events, used for debugging
      // purpose
      if (0) {
        display_inotify_event(event);
      }

      if (connect_pgie_signal == FALSE) {
        primary_gie =
            ota_appCtx->pipeline.common_elements.primary_gie_bin.primary_gie;
        if (primary_gie) {
          g_signal_connect(G_OBJECT(primary_gie), "model-updated",
                           G_CALLBACK(infer_model_updated_cb), NULL);
          connect_pgie_signal = TRUE;
        } else {
          if (log_level >= LOG_LVL_WARN) {
            printf("Gstreamer pipeline element nvinfer is yet to be created or "
                   "invalid\n");
          }
          continue;
        }
      }

      if (event->len) {
        if (event->mask & IN_MOVED_TO) {
          if (strstr("..data", event->name)) {
            memset(&ota_appCtx->override_config, 0,
                   sizeof(ota_appCtx->override_config));
            if (!IS_YAML(ota_ds_config_file)) {
              if (!parse_config_file(&ota_appCtx->override_config,
                                     ota_ds_config_file)) {
                NVGSTDS_ERR_MSG_V("Failed to parse config file '%s'",
                                  ota_ds_config_file);
                if (log_level >= LOG_LVL_ERROR) {
                  g_print(
                      "Error: ota_handler_thread: Failed to parse config file "
                      "'%s'",
                      ota_ds_config_file);
                }
              } else {
                apply_ota(ota_appCtx);
              }
            } else if (IS_YAML(ota_ds_config_file)) {
              if (!parse_config_file_yaml(&ota_appCtx->override_config,
                                          ota_ds_config_file)) {
                NVGSTDS_ERR_MSG_V("Failed to parse config file '%s'",
                                  ota_ds_config_file);
                if (log_level >= LOG_LVL_ERROR) {
                  g_print(
                      "Error: ota_handler_thread: Failed to parse config file "
                      "'%s'",
                      ota_ds_config_file);
                }
              } else {
                apply_ota(ota_appCtx);
              }
            }
          }
        }
        if (event->mask & IN_CLOSE_WRITE) {
          if (!(event->mask & IN_ISDIR)) {
            if (strstr(ota_ds_config_file, event->name)) {
              if (log_level >= LOG_LVL_DEBUG) {
                g_print("File %s modified.\n", event->name);
              }

              memset(&ota_appCtx->override_config, 0,
                     sizeof(ota_appCtx->override_config));
              if (!IS_YAML(ota_ds_config_file)) {
                if (!parse_config_file(&ota_appCtx->override_config,
                                       ota_ds_config_file)) {
                  NVGSTDS_ERR_MSG_V("Failed to parse config file '%s'",
                                    ota_ds_config_file);
                  if (log_level >= LOG_LVL_ERROR) {
                    g_print("Error: ota_handler_thread: Failed to parse config "
                            "file "
                            "'%s'",
                            ota_ds_config_file);
                  }
                } else {
                  apply_ota(ota_appCtx);
                }
              } else if (IS_YAML(ota_ds_config_file)) {
                if (!parse_config_file_yaml(&ota_appCtx->override_config,
                                            ota_ds_config_file)) {
                  NVGSTDS_ERR_MSG_V("Failed to parse config file '%s'",
                                    ota_ds_config_file);
                  if (log_level >= LOG_LVL_ERROR) {
                    g_print("Error: ota_handler_thread: Failed to parse config "
                            "file "
                            "'%s'",
                            ota_ds_config_file);
                  }
                } else {
                  apply_ota(ota_appCtx);
                }
              }
            }
          }
        }
      }
      i += INOTIFY_EVENT_SIZE + event->len;
    }
  }
done:
  inotify_rm_watch(ota_appCtx->ota_inotify_fd, ota_appCtx->ota_watch_desc);
  close(ota_appCtx->ota_inotify_fd);

  free(real_path_ds_config_file);
  g_free(ota_dir);

  g_free(ota);
  return NULL;
}

/** @} imported from deepstream-app as is */

int main(int argc, char *argv[]) {
  testAppCtx = (TestAppCtx *)g_malloc0(sizeof(TestAppCtx));
  GOptionContext *ctx = NULL;
  GOptionGroup *group = NULL;
  GError *error = NULL;
  guint i;
  OTAInfo *otaInfo = NULL;

  ctx = g_option_context_new("Nvidia DeepStream Fewshot Learning");
  group = g_option_group_new("abc", NULL, NULL, NULL, NULL);
  g_option_group_add_entries(group, entries);

  g_option_context_set_main_group(ctx, group);
  g_option_context_add_group(ctx, gst_init_get_option_group());

  GST_DEBUG_CATEGORY_INIT(NVDS_APP, "NVDS_APP", 0, NULL);

  if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
    NVGSTDS_ERR_MSG_V("%s", error->message);
    g_print("%s", g_option_context_get_help(ctx, TRUE, NULL));
    return -1;
  }

  if (log_level >= LOG_LVL_INFO) {
    g_print("Starting Deepstream FSL App\n");
    g_print("Tiled text: %d\n", show_bbox_text);
    g_print("Playback UTC: %d\n", playback_utc);
    g_print("PGIE model used: %d\n", model_used);
    g_print("no-force-tcp: %d\n", force_tcp);
    g_print("Log level: %d\n", log_level);
    g_print("Message rate: %d\n", message_rate);
    g_print("target-class: %d\n", target_class);
  }

  if (log_level == 99 || log_level == 100) {
    show_bbox_text = TRUE;
  }

  if (print_version) {
    g_print("deepstream-test5-app version %d.%d.%d\n", NVDS_APP_VERSION_MAJOR,
            NVDS_APP_VERSION_MINOR, NVDS_APP_VERSION_MICRO);
    return 0;
  }

  if (print_dependencies_version) {
    g_print("deepstream-test5-app version %d.%d.%d\n", NVDS_APP_VERSION_MAJOR,
            NVDS_APP_VERSION_MINOR, NVDS_APP_VERSION_MICRO);
    return 0;
  }

  if (cfg_files) {
    num_instances = g_strv_length(cfg_files);
  }
  if (input_files) {
    num_input_files = g_strv_length(input_files);
  }

  if (!cfg_files || num_instances == 0) {
    NVGSTDS_ERR_MSG_V("Specify config file with -c option");
    return_value = -1;
    goto done;
  }

  for (i = 0; i < num_instances; i++) {
    appCtx[i] = (AppCtx *)g_malloc0(sizeof(AppCtx));
    appCtx[i]->person_class_id = -1;
    appCtx[i]->car_class_id = -1;
    appCtx[i]->index = i;
    appCtx[i]->active_source_index = -1;
    if (show_bbox_text) {
      appCtx[i]->show_bbox_text = TRUE;
    }

    if (input_files && input_files[i]) {
      appCtx[i]->config.multi_source_config[0].uri =
          g_strdup_printf("file://%s", input_files[i]);
      g_free(input_files[i]);
    }

    if (IS_YAML(cfg_files[i])) {
      if (!parse_config_file_yaml(&appCtx[i]->config, cfg_files[i])) {
        NVGSTDS_ERR_MSG_V("Failed to parse config file '%s'", cfg_files[i]);
        appCtx[i]->return_value = -1;
        goto done;
      }
    } else {
      if (!parse_config_file(&appCtx[i]->config, cfg_files[i])) {
        NVGSTDS_ERR_MSG_V("Failed to parse config file '%s'", cfg_files[i]);
        appCtx[i]->return_value = -1;
        goto done;
      }
    }

    if (override_cfg_file && override_cfg_file[i]) {
      if (!g_file_test(
              override_cfg_file[i],
              (GFileTest)(G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK))) {
        if (log_level >= LOG_LVL_FATAL) {
          g_print("Override file %s does not exist, quitting...\n",
                  override_cfg_file[i]);
        }
        appCtx[i]->return_value = -1;
        goto done;
      }
      otaInfo = (OTAInfo *)g_malloc0(sizeof(OTAInfo));
      otaInfo->appCtx = appCtx[i];
      otaInfo->override_cfg_file = override_cfg_file[i];
      appCtx[i]->ota_handler_thread =
          g_thread_new("ota-handler-thread", ota_handler_thread, otaInfo);
    }
  }

  for (i = 0; i < num_instances; i++) {
    for (guint j = 0; j < appCtx[i]->config.num_source_sub_bins; j++) {
      /** Force the source (applicable only if RTSP)
       * to use TCP for RTP/RTCP channels.
       * forcing TCP to avoid problems with UDP port usage from within docker-
       * container.
       * The UDP RTCP channel when run within docker had issues receiving
       * RTCP Sender Reports from server
       */
      if (force_tcp)
        appCtx[i]->config.multi_source_config[j].select_rtp_protocol = 0x04;
    }
    if (!create_pipeline(appCtx[i], bbox_generated_probe_after_analytics, NULL,
                         perf_cb, overlay_graphics)) {
      NVGSTDS_ERR_MSG_V("Failed to create pipeline");
      return_value = -1;
      goto done;
    }
    /** Now add probe to RTPSession plugin src pad */
    for (guint j = 0; j < appCtx[i]->pipeline.multi_src_bin.num_bins; j++) {
      testAppCtx->streams[j].id = j;
    }
    /** In test5 app, as we could have several sources connected
     * for a typical IoT use-case, raising the nvstreammux's
     * buffer-pool-size to 16 */
    g_object_set(appCtx[i]->pipeline.multi_src_bin.streammux,
                 "buffer-pool-size", STREAMMUX_BUFFER_POOL_SIZE, NULL);
  }
  g_img_meta_consumer = create_image_meta_consumer();
  NvDsImageSave nvds_imgsave = appCtx[0]->config.image_save_config;
  if (nvds_imgsave.enable) {
    bool can_start = true;
    if (!nvds_imgsave.output_folder_path) {
      fprintf(stderr, "Consumer not started => consider adding "
                      "output-folder-path=./my/path to [img-save]\n");
      can_start = false;
    }
    if (!nvds_imgsave.frame_to_skip_rules_path) {
      fprintf(stderr,
              "Consumer not started => consider adding "
              "frame-to-skip-rules-path=./my/path/to/file.csv to [img-save]\n");
      can_start = false;
    }
    if (nvds_imgsave.second_to_skip_interval <= 0) {
      printf("[WARNING] second-to-skip-interval value should be a positive "
             "integer. Setting to Default.\n");
      nvds_imgsave.second_to_skip_interval = 600;
    }
    if (can_start) {
      /* Initiating the encode process for images. Each init function creates a
       * context on the specified gpu and can then be used to encode images.
       * Multiple contexts (even on different gpus) can also be initialized
       * according to user requirements. Only one is shown here for
       * demonstration purposes. */
      image_meta_consumer_init(
          g_img_meta_consumer, nvds_imgsave.gpu_id,
          nvds_imgsave.output_folder_path,
          nvds_imgsave.frame_to_skip_rules_path, nvds_imgsave.min_confidence,
          nvds_imgsave.max_confidence, nvds_imgsave.min_box_width,
          nvds_imgsave.min_box_height, nvds_imgsave.save_image_full_frame,
          nvds_imgsave.save_image_cropped_object,
          nvds_imgsave.second_to_skip_interval, MAX_SOURCE_BINS);
    }
    if (image_meta_consumer_is_stopped(g_img_meta_consumer)) {
      fprintf(stderr, "Consumer could not be started => exiting...\n\n");
      return_value = -1;
      goto done;
    }

  } else {
    fprintf(stderr,
            "Consumer not started => consider setting enable=1 "
            "or adding [img-save] part in config file. (example below)\n"
            "[img-save]\n"
            "enable=1\n"
            "gpu_id=0\n"
            "output-folder-path=./output\n"
            "save-img-cropped-obj=0\n"
            "save-img-full-frame=1\n"
            "frame-to-skip-rules-path=capture_time_rules.csv\n"
            "second-to-skip-interval=600\n"
            "min-confidence=0.9\n"
            "max-confidence=1.0\n"
            "min-box-width=5\n"
            "min-box-height=5\n\n");

    return_value = -1;
    goto done;
  }
  main_loop = g_main_loop_new(NULL, FALSE);

  _intr_setup();
  g_timeout_add(400, check_for_interrupt, NULL);

  g_mutex_init(&disp_lock);
  display = XOpenDisplay(NULL);
  for (i = 0; i < num_instances; i++) {
    guint j;

    if (!show_bbox_text) {
      GstElement *nvosd = appCtx[i]->pipeline.instance_bins[0].osd_bin.nvosd;
      g_object_set(G_OBJECT(nvosd), "display-text", FALSE, NULL);
    }

    if (gst_element_set_state(appCtx[i]->pipeline.pipeline, GST_STATE_PAUSED) ==
        GST_STATE_CHANGE_FAILURE) {
      NVGSTDS_ERR_MSG_V("Failed to set pipeline to PAUSED");
      return_value = -1;
      goto done;
    }

    for (j = 0; j < appCtx[i]->config.num_sink_sub_bins; j++) {
      XTextProperty xproperty;
      gchar *title;
      guint width, height;
      XSizeHints hints = {0};

      if (!GST_IS_VIDEO_OVERLAY(
              appCtx[i]->pipeline.instance_bins[0].sink_bin.sub_bins[j].sink)) {
        continue;
      }

      if (!display) {
        NVGSTDS_ERR_MSG_V("Could not open X Display");
        return_value = -1;
        goto done;
      }

      if (appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.width)
        width =
            appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.width;
      else
        width = appCtx[i]->config.tiled_display_config.width;

      if (appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.height)
        height =
            appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.height;
      else
        height = appCtx[i]->config.tiled_display_config.height;

      width = (width) ? width : DEFAULT_X_WINDOW_WIDTH;
      height = (height) ? height : DEFAULT_X_WINDOW_HEIGHT;

      hints.flags = PPosition | PSize;
      hints.x =
          appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.offset_x;
      hints.y =
          appCtx[i]->config.sink_bin_sub_bin_config[j].render_config.offset_y;
      hints.width = width;
      hints.height = height;

      windows[i] = XCreateSimpleWindow(
          display, RootWindow(display, DefaultScreen(display)), hints.x,
          hints.y, width, height, 2, 0x00000000, 0x00000000);

      XSetNormalHints(display, windows[i], &hints);

      if (num_instances > 1)
        title = g_strdup_printf(APP_TITLE "-%d", i);
      else
        title = g_strdup(APP_TITLE);
      if (XStringListToTextProperty((char **)&title, 1, &xproperty) != 0) {
        XSetWMName(display, windows[i], &xproperty);
        XFree(xproperty.value);
      }

      XSetWindowAttributes attr = {0};
      if ((appCtx[i]->config.tiled_display_config.enable &&
           appCtx[i]->config.tiled_display_config.rows *
                   appCtx[i]->config.tiled_display_config.columns ==
               1) ||
          (appCtx[i]->config.tiled_display_config.enable == 0)) {
        attr.event_mask = KeyRelease;
      } else if (appCtx[i]->config.tiled_display_config.enable) {
        attr.event_mask = ButtonPress | KeyRelease;
      }
      XChangeWindowAttributes(display, windows[i], CWEventMask, &attr);

      Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
      if (wmDeleteMessage != None) {
        XSetWMProtocols(display, windows[i], &wmDeleteMessage, 1);
      }
      XMapRaised(display, windows[i]);
      XSync(display, 1); // discard the events for now
      gst_video_overlay_set_window_handle(
          GST_VIDEO_OVERLAY(
              appCtx[i]->pipeline.instance_bins[0].sink_bin.sub_bins[j].sink),
          (gulong)windows[i]);
      gst_video_overlay_expose(GST_VIDEO_OVERLAY(
          appCtx[i]->pipeline.instance_bins[0].sink_bin.sub_bins[j].sink));
      if (!x_event_thread)
        x_event_thread =
            g_thread_new("nvds-window-event-thread", nvds_x_event_thread, NULL);
    }
  }

  /* Dont try to set playing state if error is observed */
  if (return_value != -1) {
    for (i = 0; i < num_instances; i++) {
      if (gst_element_set_state(appCtx[i]->pipeline.pipeline,
                                GST_STATE_PLAYING) ==
          GST_STATE_CHANGE_FAILURE) {
        g_print("\ncan't set pipeline to playing state.\n");
        return_value = -1;
        goto done;
      }
    }
  }

  print_runtime_commands();

  changemode(1);

  g_timeout_add(40, event_thread_func, NULL);
  g_main_loop_run(main_loop);

  changemode(0);

done:

  g_print("Quitting\n");
  for (i = 0; i < num_instances; i++) {
    if (appCtx[i] == NULL) continue;

    if (appCtx[i]->return_value == -1) return_value = -1;

    destroy_pipeline(appCtx[i]);

    if (appCtx[i]->ota_handler_thread && override_cfg_file[i]) {
      inotify_rm_watch(appCtx[i]->ota_inotify_fd, appCtx[i]->ota_watch_desc);
      g_thread_join(appCtx[i]->ota_handler_thread);
    }

    g_mutex_lock(&disp_lock);
    if (windows[i]) XDestroyWindow(display, windows[i]);
    windows[i] = 0;
    g_mutex_unlock(&disp_lock);

    g_free(appCtx[i]);
  }

  g_mutex_lock(&disp_lock);
  if (display) XCloseDisplay(display);
  display = NULL;
  g_mutex_unlock(&disp_lock);
  g_mutex_clear(&disp_lock);

  if (main_loop) {
    g_main_loop_unref(main_loop);
  }

  if (ctx) {
    g_option_context_free(ctx);
  }

  if (return_value == 0) {
    g_print("App run successful\n");
  } else {
    g_print("App run failed\n");
  }

  gst_deinit();

  if (tracker_reid_store_age > 0) {
    destroy_embedding_queue();
  }
  g_free(testAppCtx);

  return return_value;

  return 0;
}

static gchar *get_first_result_label(NvDsClassifierMeta *classifierMeta) {
  GList *n;
  for (n = classifierMeta->label_info_list; n != NULL; n = n->next) {
    NvDsLabelInfo *labelInfo = (NvDsLabelInfo *)(n->data);
    if (labelInfo->result_label[0] != '\0') {
      return g_strdup(labelInfo->result_label);
    }
  }
  return NULL;
}

static void schema_fill_sample_sgie_vehicle_metadata(NvDsObjectMeta *obj_params,
                                                     NvDsVehicleObject *obj) {
  if (!obj_params || !obj) {
    return;
  }

  /** The JSON obj->classification, say type, color, or make
   * according to the schema shall have null (unknown)
   * classifications (if the corresponding sgie failed to provide a label)
   */
  obj->type = NULL;
  obj->make = NULL;
  obj->model = NULL;
  obj->color = NULL;
  obj->license = NULL;
  obj->region = NULL;

  GList *l;
  for (l = obj_params->classifier_meta_list; l != NULL; l = l->next) {
    NvDsClassifierMeta *classifierMeta = (NvDsClassifierMeta *)(l->data);
    switch (classifierMeta->unique_component_id) {
    case SECONDARY_GIE_VEHICLE_TYPE_UNIQUE_ID:
      obj->type = get_first_result_label(classifierMeta);
      break;
    case SECONDARY_GIE_VEHICLE_COLOR_UNIQUE_ID:
      obj->color = get_first_result_label(classifierMeta);
      break;
    case SECONDARY_GIE_VEHICLE_MAKE_UNIQUE_ID:
      obj->make = get_first_result_label(classifierMeta);
      break;
    default:
      break;
    }
  }
}
