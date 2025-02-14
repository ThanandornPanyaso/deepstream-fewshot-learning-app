#include "image_meta_consumer_wrapper.h"
#include "image_meta_consumer.h"  // The original C++ header

#include <string>

// Wrapper struct to hold the actual C++ object
struct ImageMetaConsumerWrapper {
    ImageMetaConsumer* consumer;
};

// Function to retrieve the actual ImageMetaConsumer instance
ImageMetaConsumer* image_meta_consumer_get_instance(ImageMetaConsumerWrapper* wrapper) {
    return wrapper ? wrapper->consumer : nullptr;
}
// Create and destroy
ImageMetaConsumerWrapper* create_image_meta_consumer() {
    ImageMetaConsumerWrapper* wrapper = new ImageMetaConsumerWrapper();
    wrapper->consumer = new ImageMetaConsumer();
    return wrapper;
}

void destroy_image_meta_consumer(ImageMetaConsumerWrapper* wrapper) {
    if (wrapper) {
        delete wrapper->consumer;
        delete wrapper;
    }
}

// Initialize
void image_meta_consumer_init(ImageMetaConsumerWrapper* wrapper, 
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
                              unsigned source_nb) {
    if (wrapper && wrapper->consumer) {
        wrapper->consumer->init(gpu_id, 
                                std::string(output_folder_path), 
                                std::string(frame_to_skip_rules_path),
                                min_box_confidence, 
                                max_box_confidence,
                                min_box_width, 
                                min_box_height,
                                save_full_frame_enabled, 
                                save_cropped_obj_enabled,
                                seconds_to_skip_interval, 
                                source_nb);
    }
}

// Add metadata
void image_meta_consumer_add_meta_csv(ImageMetaConsumerWrapper* wrapper, const char* meta) {
    if (wrapper && wrapper->consumer) {
        wrapper->consumer->add_meta_csv(std::string(meta));
    }
}

// Stop
void image_meta_consumer_stop(ImageMetaConsumerWrapper* wrapper) {
    if (wrapper && wrapper->consumer) {
        wrapper->consumer->stop();
    }
}

// Check if stopped
int image_meta_consumer_is_stopped(ImageMetaConsumerWrapper* wrapper) {
    if (wrapper && wrapper->consumer) {
        return wrapper->consumer->get_is_stopped() ? 1 : 0;
    }
    return 0;
}
