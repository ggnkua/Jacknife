if [ "$1" == "GUI" ]; then
    # Linux
    GUI_FLAGS=$(pkg-config --cflags gtk+-2.0)
    GUI_FLAGS+=" -DGUI_CODE"
fi
echo $GUI_FLAGS
gcc dllmain.c dosfs-1.03/dosfs.c -shared -fpic -o jacknife.wcx $GUI_FLAGS
