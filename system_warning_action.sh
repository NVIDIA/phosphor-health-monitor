#!/bin/bash

ARGUMENT="$1"

# Extract threshold and resource using space as the delimiter
RESOURCE=$(echo "$ARGUMENT" | awk '{print $1}')
STORAGEPATH=$(echo "$ARGUMENT" | awk '{print $2}')

echo "Threshold: $THRESHOLD"
echo "Resource: $STORAGEPATH"

# Check for different cases based on threshold and resource
if [[ "$RESOURCE" == "Storage_"* ]]; then
    echo "Warning threshold hit for resource $RESOURCE on $STORAGEPATH "
    busctl call xyz.openbmc_project.Logging /xyz/openbmc_project/logging xyz.openbmc_project.Logging.Create Create ssa{ss} "OpenBMC.0.4.BMCSystemResourceInfo" "xyz.openbmc_project.Logging.Entry.Level.Warning" 3 "REDFISH_MESSAGE_ID" "OpenBMC.0.4.BMCSystemResourceInfo" "REDFISH_MESSAGE_ARGS" " $RESOURCE, $STORAGEPATH" xyz.openbmc_project.Logging.Entry.Resolution "None"
else
    echo "Warning threshold hit for $RESOURCE resource"
    TOPOUTPUT=$(/bin/bash /usr/bin/run_top.sh $RESOURCE)
    echo "TOPOUTPUT: $TOPOUTPUT"
    busctl call xyz.openbmc_project.Logging /xyz/openbmc_project/logging xyz.openbmc_project.Logging.Create Create ssa{ss} "OpenBMC.0.4.BMCSystemResourceInfo" "xyz.openbmc_project.Logging.Entry.Level.Warning" 3 "REDFISH_MESSAGE_ID" "OpenBMC.0.4.BMCSystemResourceInfo" "REDFISH_MESSAGE_ARGS" " $RESOURCE, $TOPOUTPUT" xyz.openbmc_project.Logging.Entry.Resolution "None"
fi                                          
