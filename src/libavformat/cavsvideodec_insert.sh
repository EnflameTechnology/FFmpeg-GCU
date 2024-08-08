#! /bin/bash

# Add the following line to the end of the file
LINE='#define CAVS_PROFILE_JIZHUN*'
CAVS='#define CAVS_PROFILE_GUANDIAN    0x48'
IF='if (\*ptr != CAVS_PROFILE_JIZHUN)'
IF_REPLACE='if (*ptr != CAVS_PROFILE_JIZHUN \&\& *ptr != CAVS_PROFILE_GUANDIAN)'


FILE_CAVS='cavsvideodec.c'


 if ! grep -Fxq "$CAVS" $FILE_CAVS
 then
    sed -i "/${LINE}/a ${CAVS}" ${FILE_CAVS}
    sed -i "s/[[:space:]]${IF}/${IF_REPLACE}/g" ${FILE_CAVS}
fi
