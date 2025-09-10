FG_PATH=~/projects/FlameGraph

sudo perf record -F 99 -g -- ../bin/hm_transcode ../in/in.mp4 h264_qsv 120 4 > /dev/null
sudo perf script | $FG_PATH/stackcollapse-perf.pl | $FG_PATH/flamegraph.pl > hm_transcode_fg.svg
