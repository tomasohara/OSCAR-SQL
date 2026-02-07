# Function to retrieve application name & icon name
# on 10/02/2025, it can be OSCAR ou OSCAR20# if deb file exists, ask what to do

function remove_deb_file ()
{
  # param : $1 = debfile
  # return : 0 : ok / <> 0 : error

  deb_file=$1

  # test if the function has been called directly or from another script
  if [ "$SHLVL" = "2" ]; then
    # direct call (level 1 = bash - level 2 = script)
    flag_batch="0"
  else
    # call from at least another script
    flag_batch="1"
  fi

  if [ -f "$deb_file" ]; then
    echo "deb_file='$deb_file'"
    if [ "$flag_batch" = "0" ];then
      # if the scipt is called directly
      read -p "*** WARNING *** : A deb file exists, delete it? (y/N): "  KbdResponse
      if [[ $KbdResponse == "Y" || $KbdResponse == "y" ]]; then
        echo "- Yes -"
        echo "Deleting old file"
        rm -f "$deb_file"
        if [ "$?" = "0" ]; then
          return 0
        else
          return 1
        fi
      else
        echo "- No -"
        echo "Exiting mkOSDistDeb due to existing deb file."
        return 2
      fi
    else
      # if the script is called not directly, return error
      return 3
    fi
  else
    echo "deb file not exists"
    return 0
  fi
}



function retrieve_names ()
{
    # Modified application name code
    OSCARPRO=${PWD%/*/*}/oscar/"oscar.pro"
    assignmentcnt=$(($(grep -cE '(^|[[:space:]])TARGET[[:space:]]*=' $OSCARPRO)-1))
    if [[ $assignmentcnt -lt 1 ]]; then
        assignmentcnt=1
    fi
    PROGNAME=$(awk -F'=' -v n=$assignmentcnt '/TARGET[[:space:]]*=/{count++; if(count==n){gsub(/^[ \t]+|[ \t]+$/,"",$2); print $2}}' $OSCARPRO)
    if [ -z "$PROGNAME" ]; then
        PROGNAME="OSCAR"
    fi

    icon_name=$PROGNAME
}

# function to generates the scripts which will be included in deb file struture
function gene_script () {
  # generate script shell from 2 files
  # clean_rm
  if [ -f "clean_rm-result-NN.sh" ]; then
    rm clean_rm-result-NN.sh
  fi
  #cat clean_rm-NN.sh clean_rm-common-NN.sh > clean_rm-result-NN.sh
  cat headers.sh retrieve_appliname2.sh clean_rm-NN.sh clean_rm-common-NN.sh > clean_rm-result-NN.sh
  chmod +x clean_rm-result-NN.sh

  if [ -f "clean_rm-result-NN-test.sh" ]; then
    rm clean_rm-result-NN-test.sh
  fi
  #cat clean_rm-NN-test.sh clean_rm-common-NN.sh > clean_rm-result-NN-test.sh
  cat headers.sh retrieve_appliname2.sh clean_rm-NN-test.sh clean_rm-common-NN.sh > clean_rm-result-NN-test.sh
  chmod +x clean_rm-result-NN-test.sh

  # ln_usrbin

  if [ -f "ln_usrbin-result-NN.sh" ]; then
    rm ln_usrbin-result-NN.sh
  fi
  #cat ln_usrbin-NN.sh ln_usrbin-common-NN.sh > ln_usrbin-result-NN.sh
  cat headers.sh retrieve_appliname2.sh ln_usrbin-NN.sh ln_usrbin-common-NN.sh > ln_usrbin-result-NN.sh
  chmod +x ln_usrbin-result-NN.sh

  if [ -f "ln_usrbin-result-NN-test.sh" ]; then
    rm ln_usrbin-result-NN-test.sh
  fi
  #cat ln_usrbin-NN-test.sh ln_usrbin-common-NN.sh > ln_usrbin-result-NN-test.sh
  cat headers.sh retrieve_appliname2.sh ln_usrbin-NN-test.sh ln_usrbin-common-NN.sh > ln_usrbin-result-NN-test.sh
  chmod +x ln_usrbin-result-NN-test.sh

  # rm_usrbin
  if [ -f "rm_usrbin-result-NN.sh" ]; then
    rm rm_usrbin-result-NN.sh
  fi
  #cat rm_usrbin-NN.sh rm_usrbin-common-NN.sh > rm_usrbin-result-NN.sh
  cat headers.sh retrieve_appliname2.sh rm_usrbin-NN.sh rm_usrbin-common-NN.sh > rm_usrbin-result-NN.sh
  chmod +x rm_usrbin-result-NN.sh

  if [ -f "rm_usrbin-result-NN-test.sh" ]; then
    rm rm_usrbin-result-NN-test.sh
  fi
  #cat rm_usrbin-NN-test.sh rm_usrbin-common-NN.sh > rm_usrbin-result-NN-test.sh
  cat headers.sh retrieve_appliname2.sh rm_usrbin-NN-test.sh rm_usrbin-common-NN.sh > rm_usrbin-result-NN-test.sh
  chmod +x rm_usrbin-result-NN-test.sh
}

# function which retrieve the current OS
function getOS () {
  rel=$(lsb_release -r | awk '{print $2}')
  os=$(lsb_release -i | awk '{print $3}')
  tmp2=${os:0:3}
  echo "tmp2 = '$tmp2'"
  if [ "$tmp2" = "Ubu" ] ; then
    OSNAME=$os${rel:0:2}
  elif [ "$tmp2" = "Deb" ];then
    OSNAME=$os$rel
  elif [ "$tmp2" = "Ras" ];then
    OSNAME="RasPiOS"
  else
    OSNAME="unknown"
  fi
}

# function to retrieve package whixh names are not the same depending version or OS
function getPkg () {
    unset PKGNAME
    unset PKGVERS
    while read stat pkg ver other ;
        do
            if [[ ${stat} == "ii" ]] ; then
                PKGNAME=`awk -F: '{print $1}' <<< ${pkg}`
                PKGVERS=`awk -F. '{print $1 "." $2}' <<< ${ver}`
                break
            fi ;
        done <<< $(dpkg -l | grep $1)
}
