cd include
ln -s asm-arm asm
cd asm
ln -s arch-mv88fxx81 arch
cd ../../..
chmod -R  u+w linux-2.6.12.6
cd linux-2.6.12.6
