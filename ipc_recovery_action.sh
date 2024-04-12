#!/bin/bash

ARGUMENT="$1"

echo "ARGUMENT: $1"
# Extract threshold and servicename using space as the delimiter
SERVICE=$(echo "$ARGUMENT" | awk '{print $1}')
REASON=$(echo "$ARGUMENT" | awk '{print $2}')

UNIT="${SERVICE//\//-}"
echo "Service: $UNIT"
echo "Reason: $REASON"

echo "Critical threshold hit for $UNIT service "
#systemctl restart $UNIT
#busctl call xyz.openbmc_project.Logging /xyz/openbmc_project/logging xyz.openbmc_project.Logging.Create Create ssa{ss} "OpenBMC.0.4.ServiceRestartReason" "xyz.openbmc_project.Logging.Entry.Level.Critical" 2 "REDFISH_MESSAGE_ID" "OpenBMC.0.4.ServiceRestartReason" "REDFISH_MESSAGE_ARGS" " $UNIT, $REASON"