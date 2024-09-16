#!/bin/bash
TOPOUTPUT=""
# Set the resource type to "CPU" or "MEM"
RESOURCE=$1
#Run the 'top' command in batch mode for one iteration, limiting the output to 25 lines
top_output=$(top -b -n 1 | head -n 5 | tail -n 2)

cpu_column=$(echo "$top_output" | head -n 1 | tr -s ' ' | sed 's/^ *//' |
             awk '{for(i=1;i<=NF;i++) if($i ~ /%CPU/) print i}')
mem_column=$(echo "$top_output" | head -n 1 | tr -s ' ' | sed 's/^ *//' |
             awk '{for(i=1;i<=NF;i++) if($i ~ /%VSZ/) print i}')

#echo "CPU Column: $cpu_column"
#echo "MEM Column: $mem_column"
top_output=$(top -b -n 1 | head -n 100 | tail -n 96)
#Iterate through each line in the top output
usage=0
while IFS= read -r line; do
    # Extract the CPU usage percentage (the 8th column)
    if [[ "$RESOURCE" == "CPU"* ]]; then
        # Check if the line contains process information and CPU usage (line starts with a space)
        usage=$(echo "$line" | awk '{print $'$cpu_column'}' | awk '{ print substr( $0, 1, length($0)-1 ) }')
    else
        usage=$(echo "$line" | awk '{print $'$mem_column'}' | awk '{ print substr( $0, 1, length($0)-1 ) }')
    fi
#    echo "USAGE : $usage"
    # Check if CPU/Memory usage is greater than
    if (( usage > 6 )) ; then
#       echo "LINE : $line"
#       echo "USAGE : $usage"
        Process=$(echo "$line" | awk '{print $9 " " }')
#       echo "Processname : $Process"
        TOPOUTPUT+="$Process($usage%) "
    fi
done <<< "$top_output"
echo "$TOPOUTPUT"
