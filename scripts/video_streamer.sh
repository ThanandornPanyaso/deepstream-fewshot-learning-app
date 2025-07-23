#!/bin/bash

# Configuration
INPUT_DIR="/home/bb24902/samples/6Mar"
API_URL="http://localhost:9000/api/v1/stream/add"
CAMERA_ID_PREFIX="sensor"
VIDEO_LIST_FILE="/home/bb24902/samples/6Mar/.video_list.txt"
CURRENT_INDEX_FILE="/home/bb24902/samples/6Mar/.current_index.txt"
RETRY_DELAY=10  # seconds

# Initialize video list (call once before using next)
init_stream() {
    find "$INPUT_DIR" -type f \( -iname "Exit__*.mkv" -o -iname "Enter__*.mkv" \) | sort > "$VIDEO_LIST_FILE"
    echo 1 > "$CURRENT_INDEX_FILE"
}

# Get the next video and send POST request with retry on failure
stream_next() {
    if [[ ! -f "$VIDEO_LIST_FILE" ]] || [[ ! -f "$CURRENT_INDEX_FILE" ]]; then
        echo "Error: run init_stream first."
        return 1
    fi

    local index
    index=$(cat "$CURRENT_INDEX_FILE")
    local total
    total=$(wc -l < "$VIDEO_LIST_FILE")

    if (( index > total )); then
        echo "All videos have been processed."
        return 1
    fi

    local video
    video=$(sed -n "${index}p" "$VIDEO_LIST_FILE")
    local filename
    filename=$(basename "$video")

    local camera_name=""
    if [[ "$filename" == Exit__* ]]; then
        camera_name="Exit"
    elif [[ "$filename" == Enter__* ]]; then
        camera_name="Enter"
    else
        echo "Skipping file: $filename (unknown prefix)"
        echo $((index + 1)) > "$CURRENT_INDEX_FILE"
        return 0
    fi

    local camera_id="${CAMERA_ID_PREFIX}${index}"

    while true; do
        local created_at
        created_at=$(date -u +"%Y-%m-%dT%H:%M:%S.%3NZ")

        local payload
        payload=$(cat <<EOF
{
  "key": "sensor",
  "value": {
     "camera_id": "$camera_id",
     "camera_name": "$camera_name",
     "camera_url": "file://$video",
     "change": "camera_add",
     "metadata": {
         "resolution": "1920 x1080",
         "codec": "h265",
         "framerate": 25
     }
  },
  "headers": {
     "source": "bash",
     "created_at": "$created_at"
  }
}
EOF
        )

        echo "Posting video #$index: $filename (camera_name: $camera_name)"

        # Send POST and capture output
        response=$(curl -s -X POST "$API_URL" \
            -H "Content-Type: application/json" \
            -d "$payload")

        echo "Response: $response"

        # Check if the response contains a failure
        if echo "$response" | grep -q "STREAM_ADD_FAIL"; then
            echo "Stream add failed. Retrying in ${RETRY_DELAY}s..."
            sleep "$RETRY_DELAY"
        else
            echo "Stream added successfully."
            break
        fi
    done

    # Advance to next video
    echo $((index + 1)) > "$CURRENT_INDEX_FILE"
}

# Cleanup function to remove tracking files
cleanup_stream() {
    rm -f "$VIDEO_LIST_FILE" "$CURRENT_INDEX_FILE"
}
init_stream
IS_FINISH=0
while [[ "$IS_FINISH" -eq 0 ]]; do
    stream_next
    IS_FINISH=$?
    sleep 3
done

