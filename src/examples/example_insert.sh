#! /bin/bash

# Add the following line to the end of the file
END="EXAMPLES\-\\\$\(CONFIG_TRANSCODING_EXAMPLE\)"
HW_DECODE_TOPS='EXAMPLES-$(CONFIG_HW_DECODE_TOPS_EXAMPLE)    += hw_decode_tops\n'
DECODE_TOPS='EXAMPLES-$(CONFIG_DECODE_TOPS_EXAMPLE)       += decode_tops\n'
HW_DECODE_MULTI_TOPS='EXAMPLES-$(CONFIG_HW_DECODE_MULTI_TOPS_EXAMPLE) += hw_decode_multi_tops\n'
M_FILE='Makefile'

 if grep -Fq "topscodec" $M_FILE;then
    echo "find topscodec exit"
    exit 0
 fi

sed -E -i "/^${END}/a \
${HW_DECODE_TOPS}\
${DECODE_TOPS}\
${HW_DECODE_MULTI_TOPS}" ${M_FILE}

END="DOC_EXAMPLES\-\\\$\(CONFIG_TRANSCODING_EXAMPLE\)"
HW_DECODE_TOPS='DOC_EXAMPLES-$(CONFIG_HW_DECODE_TOPS_EXAMPLE)    += hw_decode_tops\n'
DECODE_TOPS='DOC_EXAMPLES-$(CONFIG_DECODE_TOPS_EXAMPLE)       += decode_tops\n'
HW_DECODE_MULTI_TOPS='DOC_EXAMPLES-$(CONFIG_HW_DECODE_MULTI_TOPS_EXAMPLE) += hw_decode_multi_tops\n'
M_FILE='Makefile'

sed -E -i "/${END}/a \
${HW_DECODE_TOPS}\
${DECODE_TOPS}\
${HW_DECODE_MULTI_TOPS}" ${M_FILE}
