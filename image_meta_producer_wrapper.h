#ifndef IMAGE_META_PRODUCER_WRAPPER_H
#define IMAGE_META_PRODUCER_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "image_meta_consumer_wrapper.h"

// Opaque struct for C compatibility
typedef struct ImageMetaProducerWrapper ImageMetaProducerWrapper;

// Create an ImageMetaProducer instance
ImageMetaProducerWrapper* image_meta_producer_create(ImageMetaConsumerWrapper* consumer);

// Stack object data (equivalent to stack_obj_data in C++)
bool image_meta_producer_stack_obj_data(ImageMetaProducerWrapper* producer, 
                                        float confidence, unsigned class_id, 
                                        unsigned current_frame, unsigned video_stream_nb,
                                        const char* class_name, const char* video_path,
                                        unsigned img_height, unsigned img_width,
                                        unsigned img_top, unsigned img_left);

// Send and flush object data
void image_meta_producer_send_and_flush_obj_data(ImageMetaProducerWrapper* producer);

// Generate full-frame image path
void image_meta_producer_generate_image_full_frame_path(ImageMetaProducerWrapper* producer,
                                                        unsigned stream_source_id, const char* datetime_iso8601);

// Get the stored full-frame image path
const char* image_meta_producer_get_image_full_frame_path_saved(ImageMetaProducerWrapper* producer);

// Destroy the ImageMetaProducer instance
void image_meta_producer_destroy(ImageMetaProducerWrapper* producer);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_META_PRODUCER_WRAPPER_H
