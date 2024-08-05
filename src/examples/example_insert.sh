#! /bin/bash

# Add the following line to the end of the file
END='EXAMPLES-$(CONFIG_VAAPI_TRANSCODE_EXAMPLE)   += vaapi_transcode'
HW_DECODE_TOPS='EXAMPLES-$(CONFIG_HW_DECODE_TOPS_EXAMPLE)    += hw_decode_tops\n'
DECODE_TOPS='EXAMPLES-$(CONFIG_DECODE_TOPS_EXAMPLE)       += decode_tops\n'
HW_DECODE_MULTI_TOPS='EXAMPLES-$(CONFIG_HW_DECODE_MULTI_TOPS_EXAMPLE) += hw_decode_multi_tops'

M_FILE='Makefile'

sed -i "/${END}/a \
${HW_DECODE_TOPS}\
${DECODE_TOPS}\
${HW_DECODE_MULTI_TOPS}" ${M_FILE}