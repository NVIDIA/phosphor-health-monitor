#!/bin/bash

ARGUMENT="$1"

# Extract threshold and resource using space as the delimiter
RESOURCE=$(echo "$ARGUMENT" | awk '{print $1}')
THRESHOLD=$(echo "$ARGUMENT" | awk '{print $2}')
STORAGEPATH=$(echo "$ARGUMENT" | awk '{print $3}')

echo "Threshold: $THRESHOLD"
echo "Resource: $RESOURCE"

# Check for different cases based on threshold and resource
if [[ "$THRESHOLD" == "critical" && "$RESOURCE" == "Storage_"* ]]; then
    # Add your action for critical threshold on storage resource here
    if [[ "$RESOURCE" == "Storage_LOGGING"  ]];then
        if  mountpoint $STORAGEPATH &> /dev/null;
            then
            echo "Critical threshold hit for $RESOURCE  resource on $STORAGEPATH hence erasing the mtd partition to recover the storage"
            mtdnum=$(mount -l | awk -v path="$STORAGEPATH" '$0 ~ path {split($1, arr, "/"); gsub("mtdblock", "", arr[3]); print arr[3]}')
                echo "Erasing /dev/mtd$mtdnum partition "
                systemctl stop xyz.openbmc_project.Logging.service
                systemctl stop xyz.openbmc_project.Dump.Manager.service
                flash_eraseall -q /dev/mtd$mtdnum
            fi
    else 
        echo "Critical threshold hit for $RESOURCE  resource on $STORAGEPATH hence doing factory reset to recover the storage"
        fw_setenv openbmconce factory-reset          
    fi  
    busctl call xyz.openbmc_project.Logging /xyz/openbmc_project/logging xyz.openbmc_project.Logging.Create Create ssa{ss} "OpenBMC.0.4.BMCRebootReason" "xyz.openbmc_project.Logging.Entry.Level.Informational" 2 "REDFISH_MESSAGE_ID" "OpenBMC.0.4.BMCRebootReason" "REDFISH_MESSAGE_ARGS" " Usage of Resource $RESOURCE hit critical value"
    usleep 2000000
    #Activate the below line to reboot the BMC
    #systemctl start reboot.target
elif [[ "$THRESHOLD" == "critical" ]]; then
    echo "Critical threshold hit for $RESOURCE resource rebooting BMC"
    # Add your action for critical threshold on non-storage resource here
    busctl call xyz.openbmc_project.Logging /xyz/openbmc_project/logging xyz.openbmc_project.Logging.Create Create ssa{ss} "OpenBMC.0.4.BMCRebootReason" "xyz.openbmc_project.Logging.Entry.Level.Informational" 2 "REDFISH_MESSAGE_ID" "OpenBMC.0.4.BMCRebootReason" "REDFISH_MESSAGE_ARGS" " Usage of Resource $RESOURCE hit critical value"
    usleep 2000000
    #Activate the below line to reboot the BMC
    #systemctl start reboot.target

else
    echo "Warning threshold hit for $RESOURCE resource"
    # Add your action for warning threshold here
fi
