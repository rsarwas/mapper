xcode-select --install  #only needs to be done once after an Xcode upgrade
mv /Applications/Postgres.app/Contents/Versions/9.4/bin/pg_config /Applications/Postgres.app/Contents/Versions/9.4/bin/xpg_config
mv /usr/local/lib/libFileGDBAPI.dylib /usr/local/lib/xlibFileGDBAPI.dylib
cd ~/Projects/mapper
mkdir build
cd build # or rm -rf build; mkdir build
cmake ..
make
make package
mv /usr/local/lib/xlibFileGDBAPI.dylib /usr/local/lib/libFileGDBAPI.dylib
mv /Applications/Postgres.app/Contents/Versions/9.4/bin/xpg_config /Applications/Postgres.app/Contents/Versions/9.4/bin/pg_config