
# begin common script shell
# delete all the folder not deleted by the purge command
appli_name=$PROGNAME

echo "appli_name=${appli_name}"

list=$(find /usr/share -name ${appli_name} 2>/dev/null)

for folder in $list
do
   if [ $folder != '/usr/share/OSCAR' ]
   then
      echo "to delete : '$folder'"
      rm -r $folder
   fi
done

# end common script shell

