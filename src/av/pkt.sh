IN=in.ts

make
time ./bin/hm_transcode in/$IN h264_qsv 0.00 4.00 > out/0.ts 2> out/0.log
time ./bin/hm_transcode in/$IN h264_qsv 4.00 4.00 > out/1.ts 2> out/1.log
time ./bin/hm_transcode in/$IN h264_qsv 8.00 4.00 > out/2.ts 2> out/2.log
time ./bin/hm_transcode in/$IN h264_qsv 12.00 4.00 > out/3.ts 2> out/3.log

cat out/0.log | grep "\[in\]" > out/0.log.in
cat out/0.log | grep "\[out\]" > out/0.log.out
cat out/1.log | grep "\[in\]" > out/1.log.in
cat out/1.log | grep "\[out\]" > out/1.log.out
cat out/2.log | grep "\[in\]" > out/2.log.in
cat out/2.log | grep "\[out\]" > out/2.log.out
cat out/3.log | grep "\[in\]" > out/3.log.in
cat out/3.log | grep "\[out\]" > out/3.log.out

./bin/find_keyframes in/$IN > out/$IN.pkt
./bin/find_keyframes out/0.ts > out/0.ts.pkt
./bin/find_keyframes out/1.ts > out/1.ts.pkt
./bin/find_keyframes out/2.ts > out/2.ts.pkt
./bin/find_keyframes out/3.ts > out/3.ts.pkt

cat out/$IN.pkt | grep "stream_index:0" > out/$IN.vpkt
cat out/$IN.pkt | grep "stream_index:1" > out/$IN.apkt
cat out/0.ts.pkt | grep "stream_index:0" > out/0.ts.vpkt
cat out/0.ts.pkt | grep "stream_index:1" > out/0.ts.apkt
cat out/1.ts.pkt | grep "stream_index:0" > out/1.ts.vpkt
cat out/1.ts.pkt | grep "stream_index:1" > out/1.ts.apkt
cat out/2.ts.pkt | grep "stream_index:0" > out/2.ts.vpkt
cat out/2.ts.pkt | grep "stream_index:1" > out/2.ts.apkt
cat out/3.ts.pkt | grep "stream_index:0" > out/3.ts.vpkt
cat out/3.ts.pkt | grep "stream_index:1" > out/3.ts.apkt
