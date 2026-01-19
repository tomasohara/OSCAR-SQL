# begin common script
#
# modify by untoutseul05 to search local name for Desktop Folder
# the package now suits the fhs

# application name
echo "appli_name='$appli_name', SUDO_USER='$SUDO_USER'"

desktop_folder_name=""

if [ ! -z "$SUDO_USER" ]; then
    # find real name of the Desktop folder (Bureau for xubuntu french version)
    desktop_folder_name0="/home/$SUDO_USER/Desktop"
fi

# if doesn't exist, try to find it translated name
translate_file="/home/$SUDO_USER/.config/user-dirs.dirs"
if [ -f $translate_file ]; then
    tmp_dir="" 
    if [ ! -d "$desktop_folder_name" ]; then
            tmp_dir=$(cat $translate_file | grep XDG_DESKTOP_DIR | awk -F= '{print $2}' | awk -F\" '{print $2}' | awk -F\/ '{print $2}')
    fi

    # don't overwrite if translated name or doesn't exist
    if [ -n "$tmp_dir" ];  then
        # calculate the full folder
        tmp_dir_full="/home/${SUDO_USER}/${tmp_dir}"
        if [ -d "$tmp_dir_full" ]; then
            desktop_folder_name=$tmp_dir_full
        fi
    fi

    if [ -z "$desktop_folder_name" ]; then
        desktop_folder_name=$desktop_folder_name0
    fi
    
    file="$desktop_folder_name/${appli_name}.desktop"
    if [ -f "$file" ]; then
        rm $file
    fi
fi

# clean the destination folder
file="/usr/bin/${appli_name}"
if [ -f "$file" ]; then
    rm $file
fi



