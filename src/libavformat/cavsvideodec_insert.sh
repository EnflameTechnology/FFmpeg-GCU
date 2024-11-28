#! /bin/bash

# Add the following line to the end of the file
WS='[[:space:]]*'
FILE_CAVS='cavsvideodec.c'

LINE="#define${WS}CAVS_PROFILE_JIZHUN"
CAVS="#define CAVS_PROFILE_GUANDIAN    0x48"

IF="if${WS}\(\*ptr${WS}!=${WS}CAVS_PROFILE_JIZHUN\)"
IF_REPLACE="if (*ptr != CAVS_PROFILE_JIZHUN \&\& *ptr != CAVS_PROFILE_GUANDIAN)"

 if ! grep -Fxq "$CAVS" $FILE_CAVS
 then
    sed -E -i "/${LINE}/a ${CAVS}" ${FILE_CAVS}
    sed -E -i "s/${IF}/${IF_REPLACE}/g" ${FILE_CAVS}
fi
