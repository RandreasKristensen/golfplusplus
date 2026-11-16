"""
osm_publish.py — Install a verified import into a course folder.

The converter builds and verifies a whole course in memory, then publishes it
here, so a failed import never touches the playable course. Publication stages
every file beside its target, then replaces them one at a time. It owns only
the files it is given: anything else in the folder (hand-made art, editor
touch-ups, holes an earlier import wrote and this one does not list) is left alone.

While publishing, the folder holds MARKER. A failed replacement restores the
files already replaced and removes it; a process killed mid-publication leaves
it, and verify_osm_import.py reports the course as an error until an import
completes. Re-run the import to recover.
"""

import json
import os
from pathlib import Path

MARKER = ".import_incomplete"
STAGED_SUFFIX = ".import"


def interrupted(course_dir: Path) -> bool:
    """True when a publication into this folder started and never finished."""
    return (course_dir / MARKER).exists()


def _write_synced(path: Path, data: bytes) -> None:
    with open(path, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def _staged(target: Path) -> Path:
    return target.with_name(target.name + STAGED_SUFFIX)


def _restore(target: Path, previous: bytes | None) -> None:
    if previous is None:
        target.unlink(missing_ok=True)
        return
    staged = _staged(target)
    _write_synced(staged, previous)
    os.replace(staged, target)


def publish(course_dir: Path, files: dict[str, str]) -> None:
    """
    Write `files` (paths relative to `course_dir` -> text) into the folder, in
    the given order, so list the manifest last. Raises OSError when it fails;
    the folder is then as it was unless the message says it could not be restored.
    """
    targets = {course_dir / rel: text.encode("utf-8") for rel, text in files.items()}
    previous: dict[Path, bytes | None] = {}
    replaced: list[Path] = []
    try:
        for target in targets:
            target.parent.mkdir(parents=True, exist_ok=True)
        _write_synced(course_dir / MARKER, json.dumps({"files": list(files)}, indent=2).encode("utf-8"))
        for target, data in targets.items():
            _write_synced(_staged(target), data)
            previous[target] = target.read_bytes() if target.exists() else None
        for target in targets:
            os.replace(_staged(target), target)
            replaced.append(target)
    except OSError as failure:
        try:
            for target in reversed(replaced):
                _restore(target, previous[target])
            for target in targets:
                _staged(target).unlink(missing_ok=True)
            (course_dir / MARKER).unlink(missing_ok=True)
        except OSError as restore_failure:
            raise OSError(f"{failure}; restoring the previous files failed too ({restore_failure}), "
                          f"so {course_dir} is incomplete: re-run the import") from failure
        raise
    (course_dir / MARKER).unlink()
