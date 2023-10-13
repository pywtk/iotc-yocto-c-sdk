#!/bin/bash

led_path="/sys/class/leds/usr_led/brightness"

# Check if the user provided a value
if [ $# -eq 0 ]; then
    echo "no params provided"
    exit 1
fi

# Get the value from the command line argument
value="$1"

# Write the value to the hardcoded output file
echo "$value" > "$led_path"

# Check if the write was successful
if [ $? -eq 0 ]; then
    echo "Value '$value' written to '$led_path' successfully."
else
    echo "Error writing to '$led_path'."
    exit 1
fi