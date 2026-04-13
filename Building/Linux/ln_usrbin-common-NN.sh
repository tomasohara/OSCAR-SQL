# begin common script
#
# modify by untoutseul05 to search local name for Desktop Folder
# the package now suits the fhs

# to do : home dir read from /etc/passwd
# groupe par defaut

# application name
echo "appli_name='$appli_name', SUDO_USER='$SUDO_USER'"

echo "debut : appli_name = '$appli_name', icon_name = '$icon_name'"

desktop_folder_name=""

homedir=$(cat /etc/passwd | grep $SUDO_USER | cut -d \: -f 6)
echo "homedir = '$homedir'"

defgrp=$(id -gn $SUDO_USER)
echo "defgrp = '$defgrp'"

if [ ! -z "$SUDO_USER" ]; then
    # find real name of the Desktop folder (Bureau for xubuntu french version)
    desktop_folder_name0="${homedir}/Desktop"
fi

# try to find its translated name
# Notice : there can be both english folder & translated folder
# if such a case, deal with translated folder only

translate_file="${homedir}/.config/user-dirs.dirs"
if [ -f $translate_file ]; then
    tmp_dir=""
    if [ ! -d "$desktop_folder_name" ]; then
        tmp_dir=$(cat $translate_file | grep XDG_DESKTOP_DIR | awk -F= '{print $2}' | awk -F\" '{print $2}' | awk -F\/ '{print $2}')
    fi

    # don't overwrite if translated name or doesn't exist
    if [ -n "$tmp_dir" ];  then
        # calculate the full folder
        tmp_dir_full="${homedir}/${tmp_dir}"
        if [ -d "$tmp_dir_full" ]; then
            desktop_folder_name=$tmp_dir_full
        fi
    fi
fi

if [ -z "$desktop_folder_name" ]; then
   desktop_folder_name=$desktop_folder_name0
fi

if [ -n "$desktop_folder_name" ]; then
    # info : /usr/share/applications/${appli_name}.desktop
    # copy icon file to the Desktop folder (even if it has been translated)
    file_from="/usr/share/applications/${icon_name}.desktop"
    file_to="$desktop_folder_name/${icon_name}.desktop"

    cp $file_from $file_to

    if [ -f "$file_to" ]; then
        chown $SUDO_USER:$defgrp $file_to
        chmod a+x $file_to
    fi
fi

# end common script


