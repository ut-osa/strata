#! /bin/bash

PWD=`pwd`\/;
find $PWD \( -name '*.c' -o -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.s' -o -name '*.S' -o -name '*.java' \) -type f -print > cscope.tmp
egrep -v arch cscope.tmp > cscope.files
egrep arch cscope.tmp | egrep x86 >> cscope.files
rm -rf cscope.tmp
cscope -bR

ctags="ctags --c-kinds=+defgpstux -R"
for l in `ls ./arch | egrep -v x86`;
do
	ctags+=" --exclude=arch/$l"
done

echo $ctags
$ctags
