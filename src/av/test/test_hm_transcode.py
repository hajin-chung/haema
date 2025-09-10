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


def parse_packet_str(pkt: str) -> dict[str, float]:
    pat = r"pts_time: ([-\d\.]+) dts_time: ([-\d\.]+) duration_time: ([-\d\.]+)"
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


def print_segment(segment, io, va):
    try:
        logging.info(segment[io][va][0])
        logging.info(segment[io][va][1])
        logging.info(segment[io][va][-2])
        logging.info(segment[io][va][-1])
    except Exception as e:
        logging.error(segment[io][va])
        logging.error(e)


def max_pkt(segment):
    ret = None
    mx_pts = 0

    for pkt_str in segment:
        pkt = parse_packet_str(pkt_str)
        try:
            if pkt["pts_time"] > mx_pts:
                mx_pts = pkt["pts_time"]
                ret = pkt
        except:
            logging.info("!!!!!!!!!!!!!!!!!!!!!!!!" + pkt_str)

    return ret


def min_pkt(segment):
    ret = None
    mn_pts = 100000000000.0000

    for pkt_str in segment:
        pkt = parse_packet_str(pkt_str)
        if pkt["pts_time"] < mn_pts:
            mn_pts = pkt["pts_time"]
            ret = pkt

    return ret


def test_timestamp_alignment():
    in_src = "../in/in.ts.mp4"
    duration = 20 * 60
    segment_duration = 4.0
    start = 0.0

    """
    Packet = {
        "pts_time": float,
        "dts_time": float,
        "duration_time": float,
    }
    Segment = {
            "in": { "v": list[Packet], "a": list[Packet] }
            "out": { "v": list[Packet], "a": list[Packet] }
    }
    """
    segments = []

    while start < duration:
        logging.info(f"[running hm_transcode {start} to {start+segment_duration}]")
        args = [
            "../bin/hm_transcode",
            in_src,
            "h264_qsv",
            f"{start}",
            f"{segment_duration}",
        ]
        logging.info("$ " + " ".join(args))
        res = subprocess.run(
            args,
            capture_output=True,
        )

        start += segment_duration
        stderr = res.stderr.decode("utf-8")
        open("log", "a", encoding="utf-8").write(stderr)

        segment = {"in": {"v": [], "a": []}, "out": {"v": [], "a": []}}
        segment_idx = len(segments)

        for line in stderr.splitlines():
            io = None
            va = None
            if re.search(r"\[out\]", line):
                io = "out"
            if re.search(r"\[in\]", line):
                io = "in"
            if re.search(r"stream_index: 1", line):
                va = "a"
            if re.search(r"stream_index: 0", line):
                va = "v"

            open(f"log{segment_idx}", "a", encoding="utf-8").write(line + "\n")
            if io is None or va is None:
                continue

            segment[io][va].append(line)
            open(f"log{segment_idx}.{io}.{va}", "a", encoding="utf-8").write(
                line + "\n"
            )
            open(f"log{segment_idx}.{io}", "a", encoding="utf-8").write(line + "\n")

        if segment_idx != 0:
            print_segment(segments[-1], "out", "a")
        print_segment(segment, "out", "a")

        message = ""
        if segment_idx != 0:
            last_pkt = max_pkt(segments[-1]["out"]["a"])
            curr_pkt = min_pkt(segment["out"]["a"])
            if cmp(
                curr_pkt["dts_time"] - last_pkt["dts_time"], last_pkt["duration_time"]
            ):
                message = "SUCCESS"
            else:
                message = "FAIL"
                logging.error("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
        logging.info(f"================================ {message}")

        if segment_idx != 0:
            print_segment(segments[-1], "out", "v")
        print_segment(segment, "out", "v")

        if segment_idx != 0:
            curr_pkt_str = segment["out"]["v"][0]
            next_pkt_str = segment["out"]["v"][1]
            curr_pkt = parse_packet_str(curr_pkt_str)
            next_pkt = parse_packet_str(next_pkt_str)
            curr_pkt_dts = curr_pkt["dts_time"]
            next_pkt_dts = next_pkt["dts_time"]
            dur = next_pkt_dts - curr_pkt_dts

            last_pkt = max_pkt(segments[-1]["out"]["v"])
            curr_pkt = min_pkt(segment["out"]["v"])
            last_pts = last_pkt["pts_time"]
            curr_pts = curr_pkt["pts_time"]
            diff = curr_pts - last_pts
            if cmp(diff, dur):
                message = "SUCCESS"
            else:
                message = "FAIL"
                logging.error("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
                logging.error(f"{diff} {dur} {last_pts} {curr_pts}")
        logging.info(f"================================ {message}")

        segments.append(segment)


if __name__ == "__main__":
    test_timestamp_alignment()
