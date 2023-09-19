#!/bin/bash

ARGUMENT="$1"

# Extract threshold and resource using space as the delimiter
RESOURCE=$(echo "$ARGUMENT" | awk '{print $1}')
STORAGEPATH=$(echo "$ARGUMENT" | awk '{print $2}')

echo "Resource: $RESOURCE"

# Check for different cases based on threshold and resource
if [[ "$RESOURCE" == "Storage_"* ]]; then
    # Add your action for critical threshold on storage resource here
    if [[ "$RESOURCE" == "Storage_LOGGING"  ]];then
        if  mountpoint $STORAGEPATH &> /dev/null;
            then
            echo "Critical threshold hit for $RESOURCE  resource on $STORAGEPATH hence erasing the mtd partition to recover the storage"
            mtdnum=$(mount -l | awk -v path="$STORAGEPATH" '$0 ~ path {split($1, arr, "/"); gsub("mtdblock", "", arr[3]); print arr[3]}')
               # echo "Erasing /dev/mtd$mtdnum partition "
               # systemctl stop xyz.openbmc_project.Logging.service
               #systemctl stop xyz.openbmc_project.Dump.Manager.service
               #flash_eraseall -q /dev/mtd$mtdnum
            fi
    else 
        echo "Critical threshold hit for $RESOURCE  resource on $STORAGEPATH hence doing factory reset to recover the storage"
        #fw_setenv openbmconce factory-reset
    fi  
    busctl call xyz.openbmc_project.Logging /xyz/openbmc_project/logging xyz.openbmc_project.Logging.Create Create ssa{ss} "OpenBMC.0.4.BMCSystemResourceInfo" "xyz.openbmc_project.Logging.Entry.Level.Critical" 2 "REDFISH_MESSAGE_ID" "OpenBMC.0.4.BMCSystemResourceInfo" "REDFISH_MESSAGE_ARGS" " $RESOURCE, $STORAGEPATH"
#    sleep 5
#    systemctl start reboot.target
else
    echo "Critical threshold hit for $RESOURCE resource "
    TOPOUTPUT=$(/bin/bash /usr/bin/run_top.sh $RESOURCE)
    echo "TOPOUTPUT: $TOPOUTPUT"
    busctl call xyz.openbmc_project.Logging /xyz/openbmc_project/logging xyz.openbmc_project.Logging.Create Create ssa{ss} "OpenBMC.0.4.BMCSystemResourceInfo" "xyz.openbmc_project.Logging.Entry.Level.Critical" 2 "REDFISH_MESSAGE_ID" "OpenBMC.0.4.BMCSystemResourceInfo" "REDFISH_MESSAGE_ARGS" " $RESOURCE, $TOPOUTPUT"
fi                                      
