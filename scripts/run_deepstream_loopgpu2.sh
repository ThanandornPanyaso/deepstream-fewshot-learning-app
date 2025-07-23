#!/bin/bash

OUTPUT_DIRS=(
  "/home/bb24902/runOutput/24MarOutput"
 # "/home/bb24902/gtOutput/24MarOutput"
 # "/home/bb24902/gtOutput/6MarOutput"
)
CONFIG_FILES=(
  "/home/bb24902/deepstream-fewshot-learning-app/configs/run_tests/24mar/mtmc_config.txt"
 # "/home/bb24902/deepstream-fewshot-learning-app/configs/24mar/mtmc_config.txt"
#  "/home/bb24902/deepstream-fewshot-learning-app/configs/6mar/mtmc_config.txt"
)

NUM_TASKS=${#OUTPUT_DIRS[@]}

# # Start ZooKeeper
# cd ~/kafka_2.13-3.9.0
# echo "Starting ZooKeeper..."
# bin/zookeeper-server-start.sh config/zookeeper.properties > /tmp/zookeeper.log 2>&1 &
# ZK_PID=$!
# sleep 3

# # Start Kafka
# echo "Starting Kafka..."
# bin/kafka-server-start.sh config/server.properties > /tmp/kafka.log 2>&1 &
# KAFKA_PID=$!
# sleep 5

# Loop through tasks
for ((i=0; i<$NUM_TASKS; i++)); do
  OUTPUT_DIR=${OUTPUT_DIRS[$i]}
  CONFIG_FILE=${CONFIG_FILES[$i]}

  echo "==== Task $((i+1)) ===="
  echo "Output Dir: $OUTPUT_DIR"
  echo "Config File: $CONFIG_FILE"

  mkdir -p "$OUTPUT_DIR/output" "$OUTPUT_DIR/reid" "$OUTPUT_DIR/kitti"

  # # Start Python Kafka consumer
  # echo "Starting Kafka consumer..."
  # cd ~/
  # /home/bb24902/miniconda3/envs/mtmc_analytics/bin/python save_deepstream_msgs5_arg.py \
  #   --output_dir "$OUTPUT_DIR" > "$OUTPUT_DIR/consumer.log" 2>&1 &
  # PYTHON_PID=$!

  # Start DeepStream container (foreground)
  echo "Running DeepStream..."
  cd /home/bb24902/deepstream-fewshot-learning-app/
  ./deepstream-fewshot-learning-app -c "$CONFIG_FILE" -m 1 -t 1 -l 5 \
    --message-rate 1 --tracker-reid 1 --reid-store-age 1 -b 1 -p 1

  # echo "DeepStream finished. Stopping Python Kafka consumer..."
  # kill $PYTHON_PID
  # wait $PYTHON_PID 2>/dev/null

  echo "Task $((i+1)) complete."
  echo "=========================="
done

# Stop Kafka and ZooKeeper once all tasks are done
echo "Stopping Kafka and ZooKeeper..."
bash $HOME/kafka_2.13-3.9.0/bin/kafka-server-stop.sh
wait $KAFKA_PID 2>/dev/null
bash $HOME/kafka_2.13-3.9.0/bin/zookeeper-server-stop.sh
wait $KAFKA_PID $ZK_PID 2>/dev/null

echo "✅ All tasks completed successfully."
