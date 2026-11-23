import csv
import json
import tempfile
import unittest
from pathlib import Path

from benchmark_report import MIB, budgets, read_frames, report, statistics, wddm_peaks


class BenchmarkReportTests(unittest.TestCase):
    def test_load_peak_uses_originating_frame_zero(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frames.csv"
            path.write_text("frame,view,phase,nearby,frame_ms,peak_resident\n"
                            "1,play,cold,0,3,200\n0,play,cold,0,9,100\n"
                            "2,play,warmup,0,3,200\n3,play,steady,0,3,200\n")
            groups, _, load_peak = read_frames(path)
            self.assertEqual(load_peak["max"], 100)
            self.assertEqual(max(groups["play","cold"]["peak_resident"]), 200)
            path.write_text(path.read_text().split("3,play,steady")[0])
            with self.assertRaisesRegex(ValueError, "incomplete traversals"):
                read_frames(path)

    def test_nearest_rank_and_strict_slow_thresholds(self):
        values = list(range(1, 101))
        sample = statistics(values[::-1])
        self.assertEqual([sample[k] for k in ("p50","p95","p99","max")], [50,95,99,100])
        self.assertEqual(statistics([8,16.67,17])["over_16_67"], 1)
        self.assertEqual(statistics([8,16.67,17])["over_8"], 2)
        self.assertIsNone(statistics([])["p95"])

    def test_wddm_peaks_sum_adapters_in_the_same_sample(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "gpu-memory.csv"
            self.assertEqual(wddm_peaks(path), (0,None,None))
            path.write_text("time,gpu0 Dedicated Usage,gpu1 Dedicated Usage,gpu0 Shared Usage\n"
                            "t0,12,4,9\nt1,1,20,2\npartial,99", encoding="utf-16")
            self.assertEqual(wddm_peaks(path), (2,21,9))

    def setUp(self):
        values = dict(frame_ms=5, cpu_ms=2.5, gpu_ms=3, resident=768*MIB, load_peak_resident=1024*MIB,
                      place_gpu_bytes=256*MIB, view_dynamic_bytes=256*1024, static_uploads=0, uniform_misses=0, buffer_growths=0, state_readbacks=0)
        self.stats = {key:statistics([value]) for key,value in values.items()}
        self.context = dict(grass_gpu_bytes=8*MIB,grass_decoded_bytes=16*MIB,grass_ram_min_bytes=32*MIB,grass_ram_bound_bytes=64*MIB)

    def test_exact_limits_and_missing_timestamps(self):
        self.assertFalse(budgets("play",0,self.stats,self.context)[0])
        self.stats["gpu_ms"] = statistics([])
        self.assertIn("gpu_ms",budgets("play",0,self.stats,self.context)[1])

    def test_warm_solo_work_and_memory_fail_on_one_excess_frame(self):
        for key in ("static_uploads","uniform_misses","buffer_growths","state_readbacks","view_dynamic_bytes","resident","place_gpu_bytes","load_peak_resident"):
            with self.subTest(key=key):
                stats = dict(self.stats)
                limit = stats[key]["max"]
                stats[key] = statistics([limit]*100 + [limit+1])
                self.assertIn(key+".max",budgets("play",0,stats,self.context)[0])
        self.assertEqual(budgets("play",0,self.stats,dict(grass_gpu_bytes=8*MIB+1, grass_ram_min_bytes=64*MIB+1,
            grass_decoded_bytes=64*MIB+1))[0], ["grass_gpu","grass_decoded","grass_total_ram"])

    def test_motion_and_crowd_have_distinct_operation_budgets(self):
        self.stats["peak_resident"] = statistics([1025*MIB])
        self.assertFalse(budgets("play",0,self.stats,self.context)[0])
        self.stats["static_uploads"] = statistics([1])
        self.stats["view_dynamic_bytes"] = statistics([256*1024+1])
        self.assertEqual(budgets("sweep",0,self.stats,self.context)[0], ["view_dynamic_bytes.max"])
        self.assertFalse(budgets("plaza_40",40,self.stats,self.context)[0])

    def test_upload_gate_reads_steady_frames_and_first_frames_are_reported(self):
        columns = ["frame","view","phase","nearby","frame_ms","cpu_ms","gpu_ms","cpu_SWP_ms","resident","peak_resident",
                   "gpu_bytes","load_ms","bake_ms","dynamic_bytes","static_uploads","uniform_misses","buffer_growths",
                   "state_readbacks"]
        def row(frame, phase, uploaded, swap):
            return [frame,"play",phase,0,1,1,1,swap,1,1,1,0,0,uploaded,0,0,0,0]
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            dest = out/"repeat-1"/"ry"
            dest.mkdir(parents=True)
            (out/"manifest.json").write_text(json.dumps(dict(parameters=dict(place=["ry"], repeats=1, ms=1))))
            context = dict(place="ry", swap_interval=0, drawable="1920x1080", grass_decoded_bytes=0,
                           grass_ram_min_bytes=0, grass_ram_bound_bytes=0, grass_gpu_bytes=0)
            (dest/"context.txt").write_text("".join(f"{key}={value}\n" for key, value in context.items()))
            with (dest/"frames.csv").open("w", newline="") as file:
                writer = csv.writer(file)
                writer.writerow(columns)
                writer.writerows([row(0,"cold",9_000_000,0.5), row(1,"warmup",300_000,0.5),
                                  row(2,"steady",40_000,0.7), row(3,"steady",50_000,0.9)])
            report(out)
            view = next(csv.DictReader((out/"views.csv").read_text().splitlines()))
            self.assertEqual(view["view_dynamic_bytes_max"], "50000.0")
            self.assertEqual(view["first_frame_dynamic_bytes_max"], "9000000.0")
            self.assertEqual(view["cpu_SWP_ms_max"], "0.9")
            self.assertNotIn("view_dynamic_bytes", view["failed"])
