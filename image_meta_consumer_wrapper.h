#ifndef IMAGE_META_CONSUMER_WRAPPER_H
#define IMAGE_META_CONSUMER_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ImageMetaConsumerWrapper ImageMetaConsumerWrapper;

// Function to retrieve the actual C++ ImageMetaConsumer instance
struct ImageMetaConsumer* image_meta_consumer_get_instance(ImageMetaConsumerWrapper* wrapper);

// Create and destroy an ImageMetaConsumer object
ImageMetaConsumerWrapper* create_image_meta_consumer();
void destroy_image_meta_consumer(ImageMetaConsumerWrapper* consumer);

// Initialize the ImageMetaConsumer object
void image_meta_consumer_init(ImageMetaConsumerWrapper* consumer, 
                              unsigned gpu_id, 
                              const char* output_folder_path, 
                              const char* frame_to_skip_rules_path,
                              float min_box_confidence, 
                              float max_box_confidence,
                              unsigned min_box_width, 
                              unsigned min_box_height,
                              int save_full_frame_enabled, 
                              int save_cropped_obj_enabled,
                              unsigned seconds_to_skip_interval, 
                              unsigned source_nb);

// Add metadata
void image_meta_consumer_add_meta_csv(ImageMetaConsumerWrapper* consumer, const char* meta);

// Start and stop
void image_meta_consumer_stop(ImageMetaConsumerWrapper* consumer);
int image_meta_consumer_is_stopped(ImageMetaConsumerWrapper* consumer);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_META_CONSUMER_WRAPPER_H
