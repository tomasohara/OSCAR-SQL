# Function to retrieve application name & icon name
# on 10/02/2025, it can be OSCAR ou OSCAR20

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
