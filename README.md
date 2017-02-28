# rtmp_example
how to build :
gcc -o rtmp_server rtmp_server.cpp \`pkg-config --cflags --libs librtmp\` -g -lstdc++

crtmpserver running :
sudo /usr/sbin/crtmpserver --use-implicit-console-appender /etc/crtmpserver/crtmpserver.lua

ffmpeg running :
streaming side ==> ffmpeg -re -i 20051210-w50s.flv -f flv rtmp://localhost/live/mystream

player side ==> ffplay "rtmp://localhost:1935/live/mystream live=1"
