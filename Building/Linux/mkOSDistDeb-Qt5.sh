#! /bin/bash
# No parameter is required
# This script will identify the distribution and release version
#

# add miscellous.sh as if it is a copy-paste
. ./miscellous.sh

# generate the script from sources
gene_script

ITERATION=$1
if [  -z ${ITERATION} ]; then
    ITERATION="1"
fi

#SRC=/home/$USER/OSCAR/OSCAR-code/oscar
SRC=${PWD%/*/*}/oscar


VERSION=`awk '/#define VERSION / { gsub(/"/, "", $3); print $3 }' ${SRC}/VERSION`
if [[ ${VERSION} == *-* ]]; then
    # Use ~ for prerelease information so that it sorts correctly compared to release
    # versions. See https://www.debian.org/doc/debian-policy/ch-controlfields.html#version
    IFS="-" read -r VERSION PRERELEASE <<< ${VERSION}
    if [[ ${PRERELEASE} == *rc* ]]; then
        RC=1
    fi
    VERSION="${VERSION}-${PRERELEASE}"
fi
GIT_REVISION=`awk '/#define GIT_REVISION / { gsub(/"/, "", $3); print $3 }' ${SRC}/git_info.h`
echo Version: ${VERSION}

#Build Directory name change if needed
if [ -z "$OSCAR_BUILD_DIRECTORY" ] ; then
    OSCAR_BUILD_DIRECTORY="build"
fi
echo "Build Directory: "$OSCAR_BUILD_DIRECTORY
build_folder=${PWD%/*/*/*}/$OSCAR_BUILD_DIRECTORY

# call an external function to get application name & icon name
retrieve_names

lwrcasePROGNAME=${PROGNAME,,}
appli_name=$PROGNAME
base_name=$PROGNAME
package_name=$lwrcasePROGNAME
pre_inst="tst_user.sh"

if [[ -n ${PRERELEASE}  && -z ${RC} ]] ; then
    icon_name="${icon_name}-test"
    appli_name=${appli_name}-test
    package_name=${package_name}-test
    post_inst="ln_usrbin-result-NN-test.sh"
    pre_rem="rm_usrbin-result-NN-test.sh"
    post_rem="clean_rm-result-NN-test.sh"
else
    post_inst="ln_usrbin-result-NN.sh"
    pre_rem="rm_usrbin-result-NN.sh"
    post_rem="clean_rm-result-NN.sh"
fi

# temporary folder (absolute path is better)
temp_folder="/home/$USER/tmp_deb_${appli_name}/"

# destination folder in the .deb file
dest_folder="/usr/"

# the .deb file mustn't exist
archi_tmp=$(lscpu | grep -i architecture | awk -F: {'print $2'} | tr -d " ")
echo "archi_tmp = '${archi_tmp}'"
if [ "$archi_tmp" = "x86_64" ];then
    archi="amd64"
 elif [ "$archi_tmp" = "armv7l" ];then
   archi="armhf"
elif [ "$archi_tmp" = "aarch64" ];then
   archi="arm64"
elif [ "$archi_tmp" = "i686" ];then
   archi="i386"
elif [ "$archi_tmp" = "arm64" ];then
   archi="arm64"
else
    archi="arch_unknown"
fi

# detection of the OS with version for Ubuntu
getOS
echo "osname='$OSNAME'"

deb_file="${package_name}_${VERSION}-${OSNAME}_$archi-Qt5.deb"

result=$(remove_deb_file "$deb_file")
if [ "$result" != "0"]; then
  echo "problem with deb deletion"
fi

# if deb file exists, fatal error
#if [ -f "./$deb_file" ]; then
#    echo "destination file (./$deb_file) exists. fatal error"
#    exit
#fi

## retrieve packages version for the dependencies
getPkg libqt5core
qtver=$PKGVERS
corePkg=$PKGNAME

getPkg libdouble
dblPkg=$PKGNAME

getPkg libpcre
libpcrePkg=$PKGNAME

libSQL=""
getPkg libqt5sql5t
libSQL=$PKGNAME
if [ -z "$libSQL" ]; then
    libSQL="libqt5sql5"
fi

echo "QT name version " $corePkg $qtver
echo "DblConv package " $dblPkg
echo "libcrePkg package " $libpcrePkg
echo "libqt5sql5 " $libqt5sql5

# clean folders need to create the package
if [ -d "${temp_folder}" ]; then
    rm -r ${temp_folder}
fi
mkdir ${temp_folder}
if [ ! -d "${temp_folder}" ]; then
    echo "Folder (${temp_folder}) not created : fatal error."
    exit
fi
chmod 0755 ${temp_folder}
# save current value of umask (for u=g and not g=o)
current_value=$(umask)
umask 022
mkdir ${temp_folder}/bin
mkdir ${temp_folder}/share
mkdir ${temp_folder}/share/${appli_name}
mkdir ${temp_folder}/share/doc
share_doc_folder="${temp_folder}/share/doc/${package_name}"
mkdir ${share_doc_folder}
mkdir ${temp_folder}/share/icons
mkdir ${temp_folder}/share/icons/hicolor
mkdir ${temp_folder}/share/icons/hicolor/48x48
mkdir ${temp_folder}/share/icons/hicolor/48x48/apps
mkdir ${temp_folder}/share/icons/hicolor/scalable
mkdir ${temp_folder}/share/icons/hicolor/scalable/apps
mkdir ${temp_folder}/share/applications

# must delete debug symbol in OSCAR binary file - This was replaced with 'base_name'
#strip -s -o ${temp_folder}/bin/${appli_name} ${build_folder}/oscar/${appli_name}
# strip must be present : it copies the exe ftom the temp folder to the build folder
#  without the debug symbol : it is not a simple copy.

# Notice : ${appli_name} must be use : it is ${base_name} added with -test suffix if necessary
strip -s -o ${temp_folder}/bin/${appli_name} ${build_folder}/oscar/${base_name}

echo "av copie : icon_name = '${icon_name}'"

# 2>/dev/null : errors does not appear : we don't care about them
cp -r ${build_folder}/oscar/Help ${temp_folder}/share/${appli_name} 2>/dev/null
cp -r ${build_folder}/oscar/Html ${temp_folder}/share/${appli_name} 2>/dev/null
cp -r ${build_folder}/oscar/Translations ${temp_folder}/share/${appli_name} 2>/dev/null
cp ./${icon_name}.png ${temp_folder}/share/icons/hicolor/48x48/apps/${icon_name}.png
cp ./${icon_name}.svg ${temp_folder}/share/icons/hicolor/scalable/apps/${icon_name}.svg
cp ./${icon_name}.desktop ${temp_folder}/share/applications/${icon_name}.desktop

#echo "Copyright 2019-2020 oscar-team.org <oscar@oscar-team.org>" > $share_doc_folder/copyright
#echo "Licensed under /usr/share/common-licenses/GPL-3" >> $share_doc_folder/copyright
cp ./copyright $share_doc_folder/copyright

changelog_file="./changelog"

#automatic changelog as a bad name
# need to generate one and say fpm to use it instead of create one
# it seems that it needs both of them...

# creation of the Debian changelog
echo "$appli_name (${VERSION}-${ITERATION}) whatever; urgency=medium" > $changelog_file
echo "" >> $changelog_file
echo "  * Package created with FPM." >> $changelog_file
echo "" >> $changelog_file
echo "  * See the Release Notes under Help/About menu" >> $changelog_file
echo "" >> $changelog_file
echo -n " -- oscar-team.org <oscar@oscar-team.org> " >> $changelog_file
date -Iminutes >> $changelog_file
cp $changelog_file $share_doc_folder/changelog
gzip --best $share_doc_folder/changelog

description='Open Source CPAP Analysis Reporter\nProvides graphical and statistical display of the CPAP stored data'
# trick for dummies : need to use echo -e to take care of \n (cariage return to slipt description and extra one)
description=$(echo -e $description)

echo "base_name  : '${base_name}'"
echo "appli_name (replaces) : '${package_name}'"

# restore umask value
umask $current_value

# create the .deb file (litian test show juste a warning with a man that doesn't exists : don't care about that)
fpm --input-type dir --output-type deb  \
    --prefix ${dest_folder} \
    --before-install ${pre_inst} \
    --after-install ${post_inst}   \
    --before-remove ${pre_rem}   \
    --after-remove ${post_rem} \
    --name ${package_name} --version ${VERSION} --iteration ${OSNAME} \
    --category misc               \
    --deb-priority optional \
    --maintainer " -- oscar-team.org <oscar@oscar-team.org>"   \
    --license GPL3+             \
    --vendor oscar-team.org    \
    --description "$description" \
    --url https://sleepfiles.com/OSCAR  \
    --replaces "${package_name} ( << ${VERSION})" \
    --deb-no-default-config-files   \
    --depends $dblPkg \
    --depends ${libpcrePkg} \
    --depends qttranslations5-l10n \
    --depends "${corePkg} >= ${qtver}"   \
    --depends libqt5serialport5     \
    --depends libqt5xml5            \
    --depends libqt5network5        \
    --depends libqt5gui5            \
    --depends libqt5widgets5        \
    --depends libqt5opengl5         \
    --depends libqt5printsupport5   \
    --depends libglu1-mesa          \
    --depends libgl1                \
    --depends libc6                 \
    --depends $libSQL        \
    --no-deb-generate-changes \
    -C ${temp_folder} \
    -p ${deb_file} \
    .

# Suppress the *result* files : if not, it can make trouble with git
rm *result*
