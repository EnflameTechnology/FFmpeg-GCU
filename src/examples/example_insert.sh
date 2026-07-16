#! /bin/bash

M_FILE='Makefile'

if grep -Fq "hw_decode_tops" $M_FILE;then
   echo "find topscodec exit"
   exit 0
fi

if grep -Fq "DOC_" $M_FILE;then
   PREFIX='DOC_'
fi

# Add the following line to the end of the file
END="${PREFIX}EXAMPLES\-\\\$\(CONFIG_TRANSCODING_EXAMPLE\)"
HW_DECODE_TOPS="${PREFIX}EXAMPLES-\$(CONFIG_HW_DECODE_TOPS_EXAMPLE)    += hw_decode_tops\n"
DECODE_TOPS="${PREFIX}EXAMPLES-\$(CONFIG_DECODE_TOPS_EXAMPLE)       += decode_tops\n"
HW_DECODE_MULTI_TOPS="${PREFIX}EXAMPLES-\$(CONFIG_HW_DECODE_MULTI_TOPS_EXAMPLE) += hw_decode_multi_tops\n"
ENCODE_TOPS="${PREFIX}EXAMPLES-\$(CONFIG_ENCODE_TOPS_EXAMPLE)       += encode_tops\n"
MULTI_ENCODE_TOPS="${PREFIX}EXAMPLES-\$(CONFIG_MULTI_ENCODE_TOPS_EXAMPLE)       += multi_encode_tops\n"
HW_ENCODE_TOPS="${PREFIX}EXAMPLES-\$(CONFIG_HW_ENCODE_TOPS_EXAMPLE)       += hw_encode_tops\n"

sed -E -i "1i \
${HW_DECODE_TOPS}\
${DECODE_TOPS}\
${HW_DECODE_MULTI_TOPS}\
${ENCODE_TOPS}\
${HW_ENCODE_TOPS}\
${MULTI_ENCODE_TOPS}" ${M_FILE}


