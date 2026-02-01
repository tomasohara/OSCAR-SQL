# Function to retrieve application name & icon name
# on 10/02/2025, it can be OSCAR ou OSCAR20

function retrieve_names ()
{
    # Modified application name code
    PROGNAME=$(sed -n 's/^TARGET *= *//p' $SRC/oscar.pro)
    if [ -z "$PROGNAME" ]; then
        tmpPGM=$(sed -n 's/^    TARGET *= *//p' $SRC/oscar.pro)
    fi

    if [ -n "$tmpPGM" ]; then
        tstPGM=$(echo $tmpPGM | grep "OSCAR20")
        if [ -n "$tstPGM" ]; then
            PROGNAME="OSCAR20"
            icon_name="OSCAR20"
        else
            PROGNAME="OSCAR"
            icon_name="OSCAR"
        fi
    else
       PROGNAME="OSCAR"
       icon_name="OSCAR"
    fi
}
