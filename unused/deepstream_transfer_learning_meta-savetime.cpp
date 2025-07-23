#include "gst/gstclock.h"
#include "gstnvdsmeta.h"
#include "image_meta_consumer_wrapper.h"
#include "nvbufsurface.h"
#include "deepstream_app.h"
#include "deepstream_config_file_parser.h"
#include "nvds_version.h"
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string>
#include <memory>
#include "nvds_obj_encode.h"
#include "gst-nvmessage.h"
#include "deepstream_fewshot_learning_app.h"
#include "image_meta_consumer.h"
#include "image_meta_producer.h"
#include "image_meta_consumer_wrapper.h"

GstClockTime generate_ts_rfc3339_from_ts(char *buf, int buf_size,
                                                GstClockTime ts, gchar *src_uri,
                                                gint stream_id, gboolean playback_utc, NvDsSourceType source_type, TestAppCtx *testAppCtx) {
  time_t tloc;
  struct tm tm_log;
  char strmsec[6];  //.nnnZ\0
  int ms;

  GstClockTime ts_generated;

  if (playback_utc || (source_type !=
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

  return ts_generated;
}
// Object that will contain the necessary information for metadata file creation.
// It consumes the metadata created by producers and write them into files.
static ImageMetaConsumer *g_img_meta_consumer;

/// Will save an image cropped with the dimension specified by obj_meta
/// If the path is too long, the save will not occur and an error message will be
/// diplayed.
/// @param [in] path Where the image will be saved. If no path are specified
/// a generic one is filled. The save will be where the program was launched.
/// @param [in] ctx Object containing the saving process which is launched asynchronously.
/// @param [in] ip_surf Object containing the image to save.
/// @param [in] obj_meta Object containing information about the area to crop
/// in the full image.
/// @param [in] frame_meta Object containing information about the current frame.
/// @param [in, out] obj_counter Unsigned integer counting the number of objects saved.
/// @return true if the image was saved false otherwise.
static bool save_image(const std::string &path,
                       NvBufSurface *ip_surf, NvDsObjectMeta *obj_meta,
                       NvDsFrameMeta *frame_meta, unsigned &obj_counter) {
    NvDsObjEncUsrArgs userData = {0};
    if (path.size() >= sizeof(userData.fileNameImg)) {
        std::cerr << "Folder path too long (path: " << path
                  << ", size: " << path.size() << ") could not save image.\n"
                  << "Should be less than " << sizeof(userData.fileNameImg) << " characters.";
        return false;
    }
    if (obj_meta == NULL) {
      userData.isFrame = 1;
    }
    userData.saveImg = TRUE;
    userData.attachUsrMeta = FALSE;
    path.copy(userData.fileNameImg, path.size());
    userData.fileNameImg[path.size()] = '\0';
    userData.objNum = obj_counter++;
    userData.quality = 80;

    g_img_meta_consumer->init_image_save_library_on_first_time();
    nvds_obj_enc_process(g_img_meta_consumer->get_obj_ctx_handle(),
                         &userData, ip_surf, obj_meta, frame_meta);
    return true;
}

/// Will fill a IPData with current frame and object information
/// @param [in] appCtx Information about the video stream paths.
/// @param [in] frame_meta Information about the current frame.
/// @param [in] obj_meta Information about the current object.
/// @return An IPData object containing the necessary information
/// for an ImageMetaProducer
static ImageMetaProducer::IPData make_ipdata(const AppCtx *appCtx,
                                         const NvDsFrameMeta *frame_meta,
                                         const NvDsObjectMeta *obj_meta, TestAppCtx *testAppCtx) {
    ImageMetaProducer::IPData ipdata;
    ipdata.confidence = obj_meta->confidence;
    ipdata.within_confidence = ipdata.confidence > g_img_meta_consumer->get_min_confidence()
            && ipdata.confidence < g_img_meta_consumer->get_max_confidence();
    ipdata.class_id = obj_meta->object_id; // class_id (Person class) changed to tracker id
    ipdata.class_name = obj_meta->obj_label;
    ipdata.current_frame = frame_meta->frame_num;
    ipdata.img_width = obj_meta->rect_params.width;
    ipdata.img_height = obj_meta->rect_params.height;
    ipdata.img_top = obj_meta->rect_params.top;
    ipdata.img_left = obj_meta->rect_params.left;
    ipdata.video_stream_nb = frame_meta->pad_index;
    ipdata.video_path = appCtx->config.multi_source_config[ipdata.video_stream_nb].uri;
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::ostringstream oss;
    // oss << std::put_time(std::localtime(&t), "%FT%T%z");
    gchar *timestamp;
    guint32 stream_id = frame_meta->source_id;
    GstClockTime ts = frame_meta->buf_pts;
    generate_ts_rfc3339_from_ts(timestamp,64,ts,appCtx->config.multi_source_config[stream_id].uri,
        stream_id,TRUE,appCtx->config.multi_source_config[stream_id].type,testAppCtx);
    oss << std::string(timestamp);
    ipdata.datetime = oss.str();

    ipdata.image_cropped_obj_path_saved =
                g_img_meta_consumer->make_img_path(ImageMetaConsumer::CROPPED_TO_OBJECT,
                                                  ipdata.video_stream_nb, std::to_string(ipdata.class_id)+'_'+std::to_string(ipdata.current_frame));
    return ipdata;
}

static void display_bad_confidence(float confidence){
    if(confidence < 0.0 || confidence > 1.0){
        std::cerr << "Confidence ("  << confidence << ") provided by neural network output is invalid."
        << " ( 0.0 < confidence < 1.0 is required.)\n"
        << "Please verify the content of the config files.\n";
    }
}

static bool obj_meta_is_within_confidence(const NvDsObjectMeta *obj_meta){
   return obj_meta->confidence > g_img_meta_consumer->get_min_confidence()
    && obj_meta->confidence < g_img_meta_consumer->get_max_confidence();
}

static bool obj_meta_is_above_min_confidence(const NvDsObjectMeta *obj_meta){
    return obj_meta->confidence > g_img_meta_consumer->get_min_confidence();
}

static bool obj_meta_box_is_above_minimum_dimension(const NvDsObjectMeta *obj_meta){
    return obj_meta->rect_params.width > g_img_meta_consumer->get_min_box_width()
           && obj_meta->rect_params.height > g_img_meta_consumer->get_min_box_height();
}
/// Callback function that save full images, cropped images, and their related metadata
/// into path provided by config files. Here the consumer/producer design pattern is used,
/// this allows to create locally a producer that will create metadata and send them to
/// a registered consumer. The consumer will be in charge to write into files.
/// One consumer handle the content of several producers, using a thread safe queue.
/// @param [in] appCtx App context.
/// @param [in] buf Buffer containing frames.
/// @param [in] batch_meta Object containing information about neural network output.
/// @param [in] index Not used here.
///
extern "C" void
after_pgie_image_meta_save(AppCtx *appCtx, GstBuffer *buf,
                           NvDsBatchMeta *batch_meta, guint index, ImageMetaConsumerWrapper* consumer, TestAppCtx *testAppCtx) {
    ImageMetaConsumer* consumer_ptr = image_meta_consumer_get_instance(consumer);
    g_img_meta_consumer = consumer_ptr;

    if (g_img_meta_consumer->get_is_stopped()) {
        std::cerr << "Could not save image and metadata: "
                  << "Consumer is stopped.\n";
        return;
    }

    GstMapInfo inmap = GST_MAP_INFO_INIT;
    if (!gst_buffer_map(buf, &inmap, GST_MAP_READ)) {
        std::cerr << "input buffer mapinfo failed\n";
        return;
    }
    NvBufSurface *ip_surf = (NvBufSurface *) inmap.data;
    gst_buffer_unmap(buf, &inmap);

    /// Creating an ImageMetaProducer and registering a consumer.
    ImageMetaProducer img_producer = ImageMetaProducer(*g_img_meta_consumer);

    bool at_least_one_image_saved = false;

    for (NvDsMetaList *l_frame = batch_meta->frame_meta_list; l_frame != nullptr;
         l_frame = l_frame->next) {
        NvDsFrameMeta *frame_meta = static_cast<NvDsFrameMeta *>(l_frame->data);
        unsigned source_number = frame_meta->pad_index;
        if (g_img_meta_consumer->should_save_data(source_number)) {
            g_img_meta_consumer->lock_source_nb(source_number);
            if (!g_img_meta_consumer->should_save_data(source_number)) {
                g_img_meta_consumer->unlock_source_nb(source_number);
                continue;
            }
        } else
            continue;


        /// required for `get_save_full_frame_enabled()`
        std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%FT%T%z");
        img_producer.generate_image_full_frame_path(frame_meta->pad_index, oss.str());
        bool at_least_one_metadata_saved = false;
        bool full_frame_written = false;
        unsigned obj_counter = 0;

        bool at_least_one_confidence_is_within_range = false;
        /// first loop to check if it is useful to save metadata for the current frame
        for (NvDsMetaList *l_obj = frame_meta->obj_meta_list; l_obj != nullptr;
             l_obj = l_obj->next) {
            NvDsObjectMeta *obj_meta = static_cast<NvDsObjectMeta *>(l_obj->data);
            display_bad_confidence(obj_meta->confidence);
            if (obj_meta_is_within_confidence(obj_meta)
                && obj_meta_box_is_above_minimum_dimension(obj_meta)) {
                at_least_one_confidence_is_within_range = true;
                break;
            }
        }
        
        if(at_least_one_confidence_is_within_range) {
            for (NvDsMetaList *l_obj = frame_meta->obj_meta_list; l_obj != nullptr;
                 l_obj = l_obj->next) {
                NvDsObjectMeta *obj_meta = static_cast<NvDsObjectMeta *>(l_obj->data);
                if (!obj_meta_is_above_min_confidence(obj_meta)
                    || !obj_meta_box_is_above_minimum_dimension(obj_meta))
                    continue;

                ImageMetaProducer::IPData ipdata = make_ipdata(appCtx, frame_meta, obj_meta, testAppCtx);

                /// Store temporally information about the current object in the producer
                bool data_was_stacked = img_producer.stack_obj_data(ipdata);
                /// Save a cropped image if the option was enabled
                if (data_was_stacked && g_img_meta_consumer->get_save_cropped_images_enabled())
                    at_least_one_image_saved |= save_image(ipdata.image_cropped_obj_path_saved,
                                                           ip_surf, obj_meta, frame_meta, obj_counter);
                if (data_was_stacked && !full_frame_written
                    && g_img_meta_consumer->get_save_full_frame_enabled()) {
                    unsigned dummy_counter = 0;

                    at_least_one_image_saved |= save_image(img_producer.get_image_full_frame_path_saved(),
                                                           ip_surf, NULL, frame_meta, dummy_counter);

                    full_frame_written = true;
                }
                at_least_one_metadata_saved |= data_was_stacked;
            }
        }
        /// Send information contained in the producer and empty it.
        if(at_least_one_metadata_saved) {
            img_producer.send_and_flush_obj_data();
            g_img_meta_consumer->data_was_saved_for_source(source_number);
        }
        g_img_meta_consumer->unlock_source_nb(source_number);
    }
    /// Wait for all the thread writing jpg files to be finished. (joining a thread list)
    if (at_least_one_image_saved) {
        nvds_obj_enc_finish(g_img_meta_consumer->get_obj_ctx_handle());
    }
}
