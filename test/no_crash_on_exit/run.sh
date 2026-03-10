for i in {1..100}
do
    bin/nocrashonexit
    res=$?
    if [ $res -ne 0 ]; then
        exit $res
    fi
done
