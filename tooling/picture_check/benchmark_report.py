"""Summarize one frozen full-app matrix. Exit 2 means measured budget failure."""
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
import sys

MIB = 1024**2


def statistics(values):
    values = sorted(values)
    if not values:
        return dict(samples=0, min=None, p50=None, p95=None, p99=None, max=None,
                    over_5=None, over_8=None, over_16_67=None)
    def rank(q):
        return values[math.ceil(len(values)*q)-1]
    return dict(samples=len(values), min=values[0], p50=rank(.50), p95=rank(.95),
                p99=rank(.99), max=values[-1], over_5=sum(v > 5 for v in values),
                over_8=sum(v > 8 for v in values), over_16_67=sum(v > 16.67 for v in values))


def budgets(view, nearby, stats, context):
    checks = [("frame_ms","p95",5), ("frame_ms","p99",8), ("cpu_ms","p95",2.5),
              ("gpu_ms","p95",3), ("gpu_ms","p99",4), ("resident","max",768*MIB),
              ("load_peak_resident","max",1024*MIB), ("place_gpu_bytes","max",256*MIB)]
    if not nearby:
        checks.append(("view_dynamic_bytes","max",256*1024))
        if view not in {"sweep","walk_windy_dusk","shot_rain"}:
            checks.extend((key, "max", 0) for key in ("static_uploads","uniform_misses","buffer_growths","state_readbacks"))
    failed = []
    missing = []
    for key, stat, limit in checks:
        value = stats[key][stat]
        if value is None:
            missing.append(key)
        elif value > limit:
            failed.append(key+"."+stat)
    for key, limit, label in [("grass_gpu_bytes",8,"grass_gpu"), ("grass_decoded_bytes",16,"grass_decoded"),
                               ("grass_ram_min_bytes",64,"grass_total_ram")]:
        if int(context[key]) > limit*MIB:
            failed.append(label)
        elif label == "grass_total_ram" and int(context["grass_ram_bound_bytes"]) > limit*MIB:
            missing.append("grass_total_ram_within_bound")
    return failed, missing


def read_frames(path):
    groups = defaultdict(lambda: defaultdict(list))
    counts = {}
    frames = set()
    load_peak = []
    with path.open(newline="") as file:
        for row in csv.DictReader(file):
            frame = int(row.pop("frame"))
            if frame in frames:
                raise ValueError(f"duplicate frame {frame} in {path}")
            frames.add(frame)
            if frame == 0 and row["peak_resident"]:
                load_peak.append(float(row["peak_resident"]))
            view, phase = row.pop("view"), row.pop("phase")
            if not row["frame_ms"] or float(row["frame_ms"]) <= 0:
                raise ValueError(f"missing frame duration at {frame} in {path}")
            counts[view] = int(row.pop("nearby"))
            group = groups[view,phase]
            for metric,value in row.items():
                values = group[metric]
                if value:
                    number = float(value)
                    if not math.isfinite(number):
                        raise ValueError(f"non-finite {metric} at frame {frame} in {path}")
                    values.append(number)
    if frames != set(range(len(frames))) or not groups:
        raise ValueError(f"incomplete CPU frame stream in {path}")
    for view in counts:
        if {phase for name,phase in groups if name == view} != {"cold", "warmup", "steady"}:
            raise ValueError(f"incomplete traversals of {view} in {path}")
    return groups, counts, statistics(load_peak)


def wddm_peaks(path):
    if not path.exists():
        return 0, None, None
    data = path.read_bytes()
    text = data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig", errors="replace")
    rows = iter(csv.reader(text.splitlines()))
    fields = next(rows, [])
    dedicated = [i for i, name in enumerate(fields) if name.endswith("Dedicated Usage")]
    shared = [i for i, name in enumerate(fields) if name.endswith("Shared Usage")]
    samples = []
    for row in rows:
        if dedicated and shared and len(row) == len(fields):
            try:
                samples.append((sum(float(row[i]) for i in dedicated), sum(float(row[i]) for i in shared)))
            except ValueError:
                continue
    return len(samples), max((s[0] for s in samples), default=None), max((s[1] for s in samples), default=None)


def report(out):
    evaluator = out/"benchmark_report.py"
    if not evaluator.exists():
        evaluator.write_bytes(Path(__file__).read_bytes())
    elif evaluator.read_bytes() != Path(__file__).read_bytes():
        raise SystemExit(f"{evaluator} differs from this evaluator; run the retained copy or use a new output folder")
    manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
    args = manifest["parameters"]
    places = args["place"] or sorted(json.loads(p.read_text(encoding="utf-8"))["id"] for p in (out/"snapshot/assets/world/courses").glob("*/course.json")) + [json.loads((out/"snapshot/assets/world/hub/hub.json").read_text(encoding="utf-8"))["id"]]
    timing = {"frame_ms","gpu_ms","cpu_ms"}
    rows = []
    memory = []
    hottest = {}
    worst = defaultdict(list)
    with (out/"metrics.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["repeat","place","view","phase","metric",*statistics([])])
        for rep in range(1,args["repeats"]+1):
            for place in places:
                dest = out/f"repeat-{rep}"/place
                context = dict(line.split("=",1) for line in (dest/"context.txt").read_text().splitlines())
                memory.append([rep,place,*wddm_peaks(dest/"gpu-memory.csv"),
                               *(context[key] for key in ("grass_decoded_bytes","grass_ram_min_bytes",
                                                         "grass_ram_bound_bytes","grass_gpu_bytes"))])
                groups, counts, load_peak = read_frames(dest/"frames.csv")
                gpu_peak = max((max(g["gpu_bytes"]) for g in groups.values() if g["gpu_bytes"]), default=None)
                first_frame_peaks = {view:max((max(g["dynamic_bytes"]) for (name,phase),g in groups.items()
                                               if name == view and phase != "steady" and g["dynamic_bytes"]), default=None)
                                     for view in counts}
                if place != context["place"] or context["swap_interval"] != "0" or context["drawable"] != "1920x1080":
                    raise ValueError(f"unexpected render context in {dest}")
                for (view,phase), columns in groups.items():
                    stats = {metric:statistics(values) for metric,values in columns.items()}
                    stats["load_peak_resident"] = load_peak
                    for metric, summary in stats.items():
                        fields = list(summary.values())
                        if metric not in timing and not (metric.startswith(("cpu_","gpu_")) and metric.endswith("_ms")):
                            fields[-3:] = [None]*3
                        writer.writerow([rep,place,view,phase,metric,*fields])
                    if phase != "steady":
                        continue
                    stats["place_gpu_bytes"] = {"max":gpu_peak}
                    stats["view_dynamic_bytes"] = {"max":stats["dynamic_bytes"]["max"]}
                    for metric, summary in stats.items():
                        side, _, stage = metric.partition("_")
                        if side in {"cpu","gpu"} and stage.endswith("_ms") and summary["p95"] is not None:
                            key = (side,place,view,metric)
                            if key not in hottest or summary["p95"] > hottest[key][0]:
                                hottest[key] = (summary["p95"],rep)
                    failed, missing = budgets(view,counts[view],stats,context)
                    invalid = []
                    if sum(columns["frame_ms"])+0.1 < args["ms"]:
                        invalid.append("measured_duration")
                    if not columns["gpu_ms"] or len(columns["gpu_ms"]) < .95*len(columns["frame_ms"]):
                        invalid.append("gpu_coverage")
                    if view == "pond" and not max(columns["captures"]):
                        invalid.append("pond_capture")
                    if view == "zoom_flag_night" and (not max(columns["zoom"]) or not max(columns["gpu_scene_ZOOM_ms"] or [0])):
                        invalid.append("zoom")
                    if view == "zoom_flag_night" and not max(columns["flag_lock"]):
                        invalid.append("flag_lock")
                    if view in {"zoom_flag_night","night_lamps"} and max(columns["sun_y"]) > -0.309:
                        invalid.append("night")
                    if view == "shot_rain" and (not max(columns["moving_ball"]) or not max(columns["rain"])):
                        invalid.append("rain_or_shot")
                    if view == "walk_windy_dusk" and args["ms"] >= 10000:
                        width = float(context["grass_block_width"])
                        reach = float(context["grass_reach"])
                        if all(math.floor((max(columns[k])-reach)/width) == math.floor((min(columns[k])-reach)/width)
                               for k in ("player_x","player_z")):
                            invalid.append("walk")
                    if counts[view] and any(min(columns[key]) < counts[view] for key in ("figures","balls","carts")):
                        invalid.append("crowd")
                    status = "FAIL" if failed else "INCOMPLETE" if invalid or missing else "PASS"
                    row = dict(repeat=rep,place=place,view=view,status=status,failed=" ".join(failed),
                               unavailable=" ".join(missing),invalid=" ".join(invalid),frames=len(columns["frame_ms"]),
                               gpu_samples=len(columns["gpu_ms"]), measured_ms=sum(columns["frame_ms"]))
                    for key in sorted(timing):
                        for stat in ("p50","p95","p99","max","over_8","over_16_67"):
                            row[key+"_"+stat] = stats[key][stat]
                    for stat in ("p50","p95","p99","max"):
                        row["cpu_SWP_ms_"+stat] = stats["cpu_SWP_ms"][stat]
                    for key in ("resident","load_peak_resident","peak_resident","gpu_bytes","place_gpu_bytes",
                                "dynamic_bytes","view_dynamic_bytes","load_ms","bake_ms"):
                        row[key+"_max"] = stats[key]["max"]
                    row["first_frame_dynamic_bytes_max"] = first_frame_peaks[view]
                    rows.append(row)
                    worst[place].append(row)
    with (out/"memory.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["repeat","place","wddm_samples","dedicated_peak_bytes","shared_peak_bytes",
                         "grass_decoded_bytes","grass_ram_min_bytes","grass_ram_bound_bytes","grass_gpu_bytes"])
        writer.writerows(memory)
    with (out/"views.csv").open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=rows[0])
        writer.writeheader()
        writer.writerows(rows)
    with (out/"places.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["place","worst_frame_view","frame_p95","frame_p99","cpu_p95","cpu_p99","swp_p95","swp_p99","gpu_p95","gpu_p99",
                         "steady_mib","peak_mib","logical_gpu_mib","estimated_60hz_p99_multiplier","status"])
        for place, views in worst.items():
            def maximum(key):
                return max((r[key] for r in views if r[key] is not None), default=None)
            status = "FAIL" if any(r["failed"] for r in views) else "INCOMPLETE" if any(r["status"] != "PASS" for r in views) else "PASS"
            writer.writerow([place,max(views,key=lambda r:r["frame_ms_p95"])["view"],
                             *(maximum(key+"_"+q) for key in ("frame_ms","cpu_ms","cpu_SWP_ms","gpu_ms") for q in ("p95","p99")),
                             *(maximum(k)/MIB if maximum(k) is not None else None for k in ("resident_max","peak_resident_max","place_gpu_bytes_max")),
                             16.67/maximum("frame_ms_p99"),status])
    with (out/"hottest.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["side","place","view","stage","p95_ms","repeat"])
        for key, (cost,rep) in sorted(hottest.items(), key=lambda entry: -entry[1][0]):
            writer.writerow([*key,cost,rep])
    control = { (r["repeat"],r["view"]):r for r in rows if r["place"] == "marienlyst_golfklub" }
    if control:
        with (out/"water-control.csv").open("w", newline="") as file:
            writer = csv.writer(file)
            writer.writerow(["repeat","metric","full_p95","no_water_p95","no_water_minus_full_ms"])
            for rep in range(1,args["repeats"]+1):
                for metric in sorted(timing):
                    full = control[rep,"play"][metric+"_p95"]
                    dry = control[rep,"play_no_water"][metric+"_p95"]
                    writer.writerow([rep,metric,full,dry,dry-full if dry is not None and full is not None else None])
    invalid = sum(bool(r["invalid"]) for r in rows)
    failures = sum(bool(r["failed"]) for r in rows)
    unverified = sum(bool(r["unavailable"]) for r in rows)
    diagnostic = args["ms"] < 10000 or args["repeats"] < 3 or bool(args["place"])
    verdict = "FAIL" if failures else "INCOMPLETE" if invalid or unverified else "PASS"
    print(f"Gaming-reference budgets {verdict}. {len(rows)} measured view/repeats, {failures} with budget failures, "
          f"{invalid} with incomplete coverage and {unverified} with an unverified budget. "
          f"{'Diagnostic subset.' if diagnostic else 'Full matrix.'}", flush=True)
    if failures:
        return 2
    if invalid:
        return 1
    return 1 if any(r["status"] != "PASS" for r in rows) else 0


if __name__ == "__main__":
    sys.exit(report(Path(sys.argv[1]).resolve()))
