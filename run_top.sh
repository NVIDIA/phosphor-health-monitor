#!/bin/bash
TOPOUTPUT=""
# Set the resource type to "CPU" or "MEM"
RESOURCE=$1
#Run the 'top' command in batch mode for one iteration, limiting the output to 25 lines
top_output=$(top -b -n 1 | head -n 100 | tail -n 96)
#Iterate through each line in the top output
usage=0
while IFS= read -r line; do
    # Extract the CPU usage percentage (the 7th column)
    if [[ "$RESOURCE" == "CPU"* ]]; then
        # Check if the line contains process information and CPU usage (line starts with a space)
        usage=$(echo "$line" | awk '{print $7}' | awk '{ print substr( $0, 1, length($0)-1 ) }')
    else
        usage=$(echo "$line" | awk '{print $6}' | awk '{ print substr( $0, 1, length($0)-1 ) }')
    fi
    # Check if CPU/Memory usage is greater than
    #echo $usage
    if (( usage > 6 )) ; then
	Process=$(echo "$line" | awk '{print $8 " " $7 " "}')
        TOPOUTPUT+=$Process
    fi
done <<< "$top_output"
echo "$TOPOUTPUT"

