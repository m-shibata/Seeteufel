#!/bin/bash

usage () {
    echo ""
    echo "h: left, j: back, k: forward, l: right"
    echo "s: stop, q: quit, ?: help"
}

get_speed () {
    tmp=$1
    base=50
    if [ $tmp -eq 0 ]; then
        echo "0"
        return 0
    fi

    if [ $tmp -gt 5 ]; then
        tmp=5
    fi
    if [ $tmp -lt -5 ]; then
        tmp=-5
    fi
    if [ $tmp -lt 0 ]; then
        base=-$base
    fi

    tmp=`expr $tmp \* 10`
    expr $tmp + $base

    return 0
}

CMD=./seeteufel

if [ "$UID" != "0" ]; then
    echo "needs to be root"
    exit 1
fi

trap '$CMD -- c 0 0; $CMD sh; echo ""; exit 1' 1 2 3 15

$CMD -d

gear=0

usage

while read -n1 key; do
    case $key in
        'h' )
            $CMD -- c 0 `get_speed $gear`
            ;;
        'j' )
            gear=`expr $gear - 1`
            speed=`get_speed $gear`
            $CMD -- c $speed $speed
            ;;
        'k' )
            gear=`expr $gear + 1`
            speed=`get_speed $gear`
            $CMD -- c $speed $speed
            ;;
        'l' )
            $CMD -- c `get_speed $gear` 0
            ;;
        's' )
            gear=0
            $CMD -- c 0 0
            ;;
        'q' )
            break
            ;;
        '?' | *)
            usage
    esac
done
echo ""

$CMD -- c 0 0
$CMD sh
