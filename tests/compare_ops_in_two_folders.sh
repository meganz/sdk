#!/bin/bash
# This is a script that performs the same operations in two folders
# and compares them. 
# It can be used to test a mounted ftp/webdav folder vs a local one


f1=$1
shift
f2=$1
shift

if [[ "x$f1" == "x" || "x$f2" == "x" ]]; then
echo "USAGE: $0 folder1 folder2"
exit 1
fi

function clear_folders()
{
    echo -n "cleaning ..." 
    if [[ "x$f1" != "x" ]]; then
        rm -r "$f1/"*
    fi
    
    if [[ "x$f2" != "x" ]]; then
        rm -r "$f2/"*
    fi
    echo " DONE"
}


if [[ "x$1" == "x--clear" ]]; then
clear_folders
exit
fi


if [ "$(ls -A $f1)$(ls -A $f2)" ]; then
    echo "initialization folders not empty!"
    exit 1
fi

testnumber=1

function printtitle()
{
    echo -n " Checking test $testnumber ... "
}

function test_equal()
{
    totalattempts=6
    attempts=$totalattempts
    diff -r $f1 $f2 >/dev/null 2>/dev/null
    result=$?
    while [[ $attempts -ge 0  && $result -ne 0 ]]; do
        echo -n "$attempts"
        sleep $((1*($totalattempts-$attempts)))
        attempts=$(($attempts - 1))
        diff -r $f1 $f2 >/dev/null 2>/dev/null
        result=$?
    done    
    
    if [[ $result -ne 0 ]]; then
    echo " ... FAILED"
    find $f1 $f2
    clear_folders #TODO: only if !debug ?
    exit
    fi
    echo " ... OK"
    testnumber=$(($testnumber + 1))
    #find "$f1" #TODO: only if debug
    echo "---------"
}

#Test 01
printtitle
for i in $f1 $f2; do 
#touch $i/f01.txt || exit 1 #touch is not supported in gvfsd mounted folder
echo "" > $i/f01.txt || exit 1
done
test_equal

#Test 02
printtitle
for i in $f1 $f2; do 
echo test > $i/f02.txt || exit 1
done
test_equal

testnumber=3
#Test 03
printtitle
for i in $f1 $f2; do 
#mkdir -p $i/f0{1,2}/f0{1,2}/f0{1,2,3}; #This seem to fail sometimes for ftp! why? It seems some issue with gvfs & caches (some mkdir fail before being attempted)
for x in $i/f0{1,2}{,/sf0{1,2}{,/ssf0{1,2,3}}}; do
mkdir $x || exit 1
done

done
test_equal

#Test 04
printtitle
for i in $f1 $f2; do 
cp $i/f01{,_copy}.txt || exit 1
done
test_equal

#Test 05
printtitle
for i in $f1 $f2; do 
mv $i/f0{2,_moved}.txt || exit 1
done
test_equal

#Test 06
printtitle
for i in $f1 $f2; do 
rm $i/f01_copy.txt || exit 1
done
test_equal

#Test 07
printtitle
for i in $f1 $f2; do 
cp $i/f01.txt $i/f01/ || exit 1
done
test_equal

#Test 08
printtitle
for i in $f1 $f2; do 
#echo change >> $i/f01.txt #this op is not supported in GVFS
echo change > $i/f01.txt
done
test_equal

#Test 09
printtitle
for i in $f1 $f2; do 
mv $i/f01.txt $i/f01/ || exit 1
done
test_equal

#Test 10
printtitle
for i in $f1 $f2; do 
rm -r $i/f02 || exit 1
done
test_equal

#Test 11
printtitle
for i in $f1 $f2; do 
cp -r $i/f01 $i/f01_copied || exit 1
done
test_equal

#Test 12
printtitle
for i in $f1 $f2; do 
mv $i/f01 $i/f01_moved || exit 1
done
test_equal

#Test 13
printtitle
for i in $f1 $f2; do 
mv $i/f01_moved $i/f01_copied/ || exit 1
done
test_equal

clear_folders
