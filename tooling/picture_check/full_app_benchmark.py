"""Run independent offline app processes against a frozen executable and asset tree."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

from benchmark_report import report

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = os.name == "nt"


def capture(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True, encoding="utf-8", errors="replace").strip()


def workloads():
    if WINDOWS:
        rows = csv.reader(capture("tasklist", "/fo", "csv", "/nh").splitlines())
        return {row[0].lower() for row in rows if row}
    return {Path(line.strip()).name for line in capture("ps", "-A", "-o", "comm=").splitlines()}


def competing(during=False):
    names = workloads()
    builds = {"cmake", "ninja", "make", "cc1plus", "clang", "rustc", "cargo",
              "golfpp-picture-check", "golfpp-tests", "golf++-tests", "golfpp-shot-lab", "vcr-golf",
              "cl", "link", "msbuild"}
    if not during:
        builds.add("golf++")
    return sorted(name for name in names if name.removesuffix(".exe") in builds
                  or name.lower().endswith("-win64-shipping.exe"))


def freeze(exe, out, args):
    build = out / "snapshot"
    build.mkdir()
    saved = build / exe.name
    shutil.copy2(exe, saved)
    for dll in exe.parent.glob("*.dll"):
        shutil.copy2(dll, build / dll.name)
    assets = exe.parent / "assets"
    shutil.copytree(assets if assets.is_dir() else ROOT / "assets", build / "assets")
    digest = hashlib.sha256()
    for path in sorted((build / "assets").rglob("*")):
        if path.is_file():
            digest.update(path.relative_to(build).as_posix().encode() + b"\0")
            digest.update(hashlib.sha256(path.read_bytes()).digest())
    status = capture("git", "status", "--porcelain")
    (out / "source-status.txt").write_text(status + "\n", encoding="utf-8")
    source_diff = subprocess.check_output(["git", "-c", "core.safecrlf=false", "diff", "--binary", "HEAD"], cwd=ROOT)
    (out / "source.patch").write_bytes(source_diff)
    for path in capture("git", "ls-files", "--others", "--exclude-standard").splitlines():
        if path:
            dest = out / "untracked" / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / path, dest)
    meta = dict(utc=datetime.now(timezone.utc).isoformat(), machine=platform.node(), os=platform.platform(),
                cpu=platform.processor(), git=capture("git", "rev-parse", "HEAD"), dirty=bool(status),
                assets_sha256=digest.hexdigest(), executable_sha256=hashlib.sha256(saved.read_bytes()).hexdigest(),
                parameters=vars(args), power="see per-process SDL power state", order="alternating course and view order")
    if WINDOWS:
        meta["cpu"] = capture("powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).Name")
    (out / "manifest.json").write_text(json.dumps(meta, indent=2, default=str), encoding="utf-8")
    return saved


def run_place(exe, dest, place, args, reverse):
    dest.mkdir(parents=True)
    env = {k:v for k,v in os.environ.items() if not k.startswith("GOLFPP_")}
    env.update(GOLFPP_BENCHMARK=str(dest), GOLFPP_COURSE=place, GOLFPP_WINDOW="1920x1080",
               GOLFPP_BENCHMARK_MS=str(args.ms), GOLFPP_BENCHMARK_REVERSE=str(int(reverse)))
    if args.date:
        # The course stands on this day (its trees' season); each view still pins its own sky.
        env.update(GOLFPP_GAME_TIME=f"1989-{args.date}T12")
    before = competing()
    if before:
        sys.exit(f"Competing process before {dest.name}: {before}")
    with (dest / "app.log").open("wb") as log, subprocess.Popen(
            [str(exe)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT) as proc:
        gpu_memory = None
        if WINDOWS and shutil.which("typeperf"):
            # Wait for SDL to create the GL context and its WDDM process instance.
            try:
                proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                instance = rf"\GPU Process Memory(pid_{proc.pid}_*)"
                gpu_memory = subprocess.Popen(["typeperf", instance+r"\Dedicated Usage", instance+r"\Shared Usage",
                                               "-si", "1", "-o", str(dest/"gpu-memory.csv")],
                                              cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        try:
            watch_process(proc, dest, place)
        finally:
            if gpu_memory and gpu_memory.poll() is None:
                gpu_memory.terminate()
                gpu_memory.wait()
        if proc.returncode:
            sys.exit(f"{place} exited {proc.returncode}, see {dest / 'app.log'}")


def watch_process(proc, dest, place):
    while proc.poll() is None:
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            collision = competing(during=True)
            if WINDOWS:
                clients = [line for line in capture("tasklist", "/fi", "imagename eq golf++.exe",
                                                    "/fo", "csv", "/nh").splitlines() if line.startswith('"golf++.exe"')]
                if len(clients) > 1:
                    collision.append("second golf++")
            if collision:
                proc.terminate()
                sys.exit(f"Competing process during {place}: {collision}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/release" / ("golf++.exe" if WINDOWS else "golf++"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ms", type=int, default=10000)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--reverse", action="store_true", help="start with reversed place and view order")
    parser.add_argument("--place", action="append", help="limit to this place, repeatable")
    parser.add_argument("--date", help="MM-DD: date the courses (their trees' season); the hub stays at midsummer")
    args = parser.parse_args()
    if not 1 <= args.ms <= 99999 or args.repeats < 1:
        parser.error("--ms must be 1..99999 and --repeats positive")
    out = args.output.resolve()
    if out.exists():
        parser.error("choose a fresh output directory")
    out.mkdir(parents=True)
    exe = freeze(args.exe.resolve(), out, args)
    if WINDOWS:
        sample = """$first = @{}
Get-Process | ForEach-Object { $first[$_.Id] = $_.CPU }
Start-Sleep -Seconds 2
Get-Process | Where-Object { $first.ContainsKey($_.Id) } | ForEach-Object {
    [pscustomobject]@{Name=$_.Name; Id=$_.Id; CpuSeconds=[math]::Round($_.CPU - $first[$_.Id], 3)}
} | Sort-Object CpuSeconds -Descending | Select-Object -First 12 | ConvertTo-Csv -NoTypeInformation"""
        (out/"cpu-before.csv").write_text(capture("powershell", "-NoProfile", "-Command", sample), encoding="utf-8")
    assets = exe.parent / "assets/world"
    courses = sorted(json.loads(p.read_text(encoding="utf-8"))["id"] for p in assets.glob("courses/*/course.json"))
    places = courses + [json.loads((assets / "hub/hub.json").read_text(encoding="utf-8"))["id"]]
    if args.place:
        if set(args.place) - set(places):
            parser.error("unknown place")
        places = list(dict.fromkeys(args.place))
    telemetry = None
    if shutil.which("nvidia-smi"):
        (out / "gpu-before.txt").write_text(capture("nvidia-smi"), encoding="utf-8")
        query = "timestamp,name,driver_version,pstate,temperature.gpu,clocks.gr,clocks.mem,power.draw,utilization.gpu,memory.used,clocks_event_reasons.sw_thermal_slowdown,clocks_event_reasons.hw_thermal_slowdown"
        telemetry = subprocess.Popen(["nvidia-smi", "--query-gpu="+query, "--format=csv", "-l", "1",
                                      "-f", str(out / "gpu-telemetry.csv")], cwd=ROOT)
    try:
        for rep in range(1, args.repeats + 1):
            reverse = args.reverse ^ (rep % 2 == 0)
            for place in (places[::-1] if reverse else places):
                print(f"{datetime.now():%H:%M:%S}  {rep}/{args.repeats}  {place}", flush=True)
                run_place(exe, out / f"repeat-{rep}" / place, place, args, reverse)
    finally:
        if telemetry:
            telemetry.terminate()
            telemetry.wait()
    return report(out)


if __name__ == "__main__":
    sys.exit(main())
