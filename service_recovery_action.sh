#!/bin/bash
ARGUMENT="$1"
echo "ARGUMENT: $ARGUMENT"
# Extract threshold and resource using space as the delimiter
RESOURCE=$(echo "$ARGUMENT" | awk '{print $1}')
SERVICE=$(echo "$ARGUMENT" | awk '{print $2}')
USAGE=$(echo "$ARGUMENT" | awk '{print $3}')

[ ! -z "$RESOURCE" ] && echo "Resource: $RESOURCE"
[ ! -z "$SERVICE" ] && echo "Service: $SERVICE"
[ ! -z "$USAGE" ] && echo "Usage: $USAGE"

echo "Critical threshold hit for $RESOURCE resource in $SERVICE service usage is $USAGE%"
