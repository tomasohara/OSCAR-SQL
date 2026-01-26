#----! /bin/bash
#set -e
#
# modify by untoutseul05 to search local name for Desktop Folder
# the package now suits the fhs

# application name
#appli_name="OSCAR-test"

retrieve_names

appli_name="${PROGNAME}-test"
icon_tmp=$icon_name
icon_name="${icon_tmp}-test"
