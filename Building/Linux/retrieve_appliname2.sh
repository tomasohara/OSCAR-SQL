# Function to retrieve application name & icon name
# on 10/02/2025, it can be OSCAR ou OSCAR20

function retrieve_names ()
{
  # $DPKG_MAINTSCRIPT_PACKAGE seems to contents the package name
  # VERSION file is not present on the user computer (source only)

  echo "avant affect"
  pkg=$DPKG_MAINTSCRIPT_PACKAGE
  echo "apres affect : pkg = '$pkg'"

  #test=$(echo "$pkg" | grep -i "oscar20")

  #echo "test = '$test'"

  # !!! the package is in lowercase
  #if [ -n "$test" ]; then
  if [[ "$pkg" == *"oscar20"* ]] || [[ "$pkg" == *"OSCAR20"* ]]; then
   # it is OSCAR20
   PROGNAME="OSCAR20"
   icon_name="OSCAR20"
  else
   # it is OSCAR
   PROGNAME="OSCAR"
   icon_name="OSCAR"
  fi
  echo "fin fnct"

  echo " fin fct : appli_name = '$PROGNAME', icon_name = '$icon_name'"
}
