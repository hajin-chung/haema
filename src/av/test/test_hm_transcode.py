import subprocess
import logging
import re

logging.basicConfig(
    format="%(asctime)s %(levelname)-8s %(message)s",
    level=logging.INFO,
    datefmt="%Y-%m-%d %H:%M:%S",
)


def cmp(a: float, b: float) -> bool:
    return abs(a - b) < 0.001


def parse_packet(pkt: str) -> dict[str, float]:
    pat = r"pts_time: ([\d\.]+) dts_time: ([\d\.]+) duration_time: ([\d\.]+)"
    match = re.search(pat, pkt)
    ret = {}
    if match:
        pts_time, dts_time, duration_time = match.groups()
        ret = {
            "pts_time": float(pts_time),
            "dts_time": float(dts_time),
            "duration_time": float(duration_time),
        }
    return ret


def test_timestamp_alignment():
    duration = 20 * 60
    segment_duration = 4.0
    start = 0.0
    packet_logs = []
    while start < duration:
        logging.info(f"[running hm_transcode {start} to {start+segment_duration}]")
        res = subprocess.run(
            [
                "../bin/hm_transcode",
                "../in/in.ts",
                "h264_qsv",
                f"{start}",
                f"{segment_duration}",
            ],
            capture_output=True,
        )
        start += segment_duration

        stderr = res.stderr.decode("utf-8")
        open("log", "w", encoding="utf-8").write(stderr)

        packets = {"in": {"v": [], "a": []}, "out": {"v": [], "a": []}}

        for line in stderr.splitlines():
            io = "in"
            va = "v"
            if re.search(r"\[out\]", line):
                io = "out"
            if re.search(r"stream_index: 1", line):
                va = "a"
            packets[io][va].append(line)

        segment_idx = len(packet_logs)
        if segment_idx != 0:
            logging.info(packet_logs[segment_idx - 1]["out"]["a"][0])
            logging.info(packet_logs[segment_idx - 1]["out"]["a"][-1])
        logging.info(packets["out"]["a"][0])
        logging.info(packets["out"]["a"][-1])

        message = ""
        if segment_idx != 0:
            last_pkt = parse_packet(packet_logs[segment_idx - 1]["out"]["a"][-1])
            curr_pkt = parse_packet(packets["out"]["a"][0])
            if cmp(
                curr_pkt["dts_time"] - last_pkt["dts_time"], last_pkt["duration_time"]
            ):
                message = "SUCCESS"
            else:
                message = "FAIL"
                logging.error("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
                raise
        logging.info(f"================================ {message}")

        if segment_idx != 0:
            logging.info(packet_logs[segment_idx - 1]["out"]["v"][0])
            logging.info(packet_logs[segment_idx - 1]["out"]["v"][-1])
        logging.info(packets["out"]["v"][0])
        logging.info(packets["out"]["v"][-1])

        if segment_idx != 0:
            last_pkt_str = packet_logs[segment_idx - 1]["out"]["v"][-1]
            curr_pkt_str = packets["out"]["v"][0]
            next_pkt_str = packets["out"]["v"][1]
            last_pkt = parse_packet(last_pkt_str)
            curr_pkt = parse_packet(curr_pkt_str)
            next_pkt = parse_packet(next_pkt_str)
            diff = curr_pkt["dts_time"] - last_pkt["dts_time"]
            dur = next_pkt["dts_time"] - curr_pkt["dts_time"]
            if cmp(diff, dur):
                message = "SUCCESS"
            else:
                message = "FAIL"
                logging.error("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
                logging.error(f"{diff} {dur}")
                logging.error(last_pkt_str)
                logging.error(curr_pkt_str)
                logging.error(next_pkt_str)
        logging.info(f"================================ {message}")

        packet_logs.append(packets)


if __name__ == "__main__":
    test_timestamp_alignment()
