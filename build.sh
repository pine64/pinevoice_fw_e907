if [ "$1" == "full" ]; then
    pushd ../pinevoice_fw_c906/ || exit 1
    ./go
    go_status=$?
    popd

    if [ "$go_status" -ne 0 ]; then
        echo "Compilation failed"
        exit "$go_status"
    fi
fi

scons . -j`cat /proc/cpuinfo| grep "processor"| wc -l`