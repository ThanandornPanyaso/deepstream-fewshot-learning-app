#include "image_meta_producer_wrapper.h"
#include "image_meta_producer.h"

// Wrapper struct
struct ImageMetaProducerWrapper {
    ImageMetaProducer* producer;
};

// Create an ImageMetaProducer instance
ImageMetaProducerWrapper* image_meta_producer_create(ImageMetaConsumerWrapper* consumer) {
    if (!consumer) return nullptr;
    ImageMetaConsumer* consumer_ptr = image_meta_consumer_get_instance(consumer);
    ImageMetaProducerWrapper* wrapper = new ImageMetaProducerWrapper;
    wrapper->producer = new ImageMetaProducer(*consumer_ptr);
    return wrapper;
}

// Stack object data
bool image_meta_producer_stack_obj_data(ImageMetaProducerWrapper* producer, 
                                        float confidence, unsigned class_id, 
                                        unsigned current_frame, unsigned video_stream_nb,
                                        const char* class_name, const char* video_path,
                                        unsigned img_height, unsigned img_width,
                                        unsigned img_top, unsigned img_left) {
    if (!producer || !producer->producer) return false;

    ImageMetaProducer::IPData data;
    data.confidence = confidence;
    data.class_id = class_id;
    data.current_frame = current_frame;
    data.video_stream_nb = video_stream_nb;
    data.class_name = class_name ? std::string(class_name) : "";
    data.video_path = video_path ? std::string(video_path) : "";
    data.img_height = img_height;
    data.img_width = img_width;
    data.img_top = img_top;
    data.img_left = img_left;

    return producer->producer->stack_obj_data(data);
}

// Send and flush object data
void image_meta_producer_send_and_flush_obj_data(ImageMetaProducerWrapper* producer) {
    if (producer && producer->producer) {
        producer->producer->send_and_flush_obj_data();
    }
}

// Generate full-frame image path
void image_meta_producer_generate_image_full_frame_path(ImageMetaProducerWrapper* producer,
                                                        unsigned stream_source_id, const char* datetime_iso8601) {
    if (producer && producer->producer && datetime_iso8601) {
        producer->producer->generate_image_full_frame_path(stream_source_id, std::string(datetime_iso8601));
    }
}

// Get the stored full-frame image path
const char* image_meta_producer_get_image_full_frame_path_saved(ImageMetaProducerWrapper* producer) {
    if (!producer || !producer->producer) return nullptr;
    return producer->producer->get_image_full_frame_path_saved().c_str();
}

// Destroy the ImageMetaProducer instance
void image_meta_producer_destroy(ImageMetaProducerWrapper* producer) {
    if (producer) {
        delete producer->producer;
        delete producer;
    }
}
