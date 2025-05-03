#!/bin/bash

# Default values
INPUT_FILE=""
OUTPUT_FILE=""
NEW_PATH=""

# Function to print help
print_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --input=FILE    Input JSON configuration file (required)"
    echo "  --output=FILE   Output JSON configuration file (required)"
    echo "  --path=PATH     New base path to replace \$MICROPANEL_HOME with (required)"
    echo "  --help          Display this help message and exit"
    echo ""
    echo "Example:"
    echo "  $0 --input=config-debian-iperf.json --output=config-debian-iperf-updated.json --path=/home/pi"
    echo ""
}

# Parse command line arguments
for arg in "$@"; do
    case $arg in
        --input=*)
            INPUT_FILE="${arg#*=}"
            ;;
        --output=*)
            OUTPUT_FILE="${arg#*=}"
            ;;
        --path=*)
            NEW_PATH="${arg#*=}"
            ;;
        --help)
            print_help
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            print_help
            exit 1
            ;;
    esac
done

# Check if all required arguments are provided
if [ -z "$INPUT_FILE" ] || [ -z "$OUTPUT_FILE" ] || [ -z "$NEW_PATH" ]; then
    echo "Error: Missing required arguments."
    print_help
    exit 1
fi

# Remove trailing slash from path if present
NEW_PATH="${NEW_PATH%/}"

# Check if input file exists
if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: Input file not found: $INPUT_FILE"
    exit 1
fi

echo "Configuration:"
echo "  Input file: $INPUT_FILE"
echo "  Output file: $OUTPUT_FILE"
echo "  New path: $NEW_PATH"
echo ""
echo "Replacing \$MICROPANEL_HOME with '$NEW_PATH'"

# Simple sed replacement for the variable
sed "s|\\\$MICROPANEL_HOME|$NEW_PATH|g" "$INPUT_FILE" > "$OUTPUT_FILE"

# Check if the replacement was successful
if [ $? -eq 0 ]; then
    echo "Success: Updated configuration saved to $OUTPUT_FILE"
else
    echo "Error: Failed to update configuration"
    exit 1
fi

# Optional: Count the number of replacements
COUNT=$(grep -o "$NEW_PATH" "$OUTPUT_FILE" | wc -l)
echo "Made $COUNT path replacements"

exit 0
