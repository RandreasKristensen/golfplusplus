"""Find and repair DEM flat-bottom dropouts without smoothing ordinary terrain.

Some inland water patches in DHM have a constant, grossly incorrect height.
Only enclosed plateaus with a large jump to almost their entire rim qualify.
Interpolation uses the surrounding land; mapped water is still levelled by the
game's existing pond construction. numpy/scipy are importer dependencies.
"""

import numpy as np
from scipy import ndimage as ndi
from scipy.interpolate import LinearNDInterpolator


def dropout_masks(heights, cell):
    """Local slices and masks for enclosed, repeated-height deep pits."""
    if cell > 2.0:
        return
    # Repeated values alone are normal on ponds. A steep, enclosing rim is
    # required too. Inspect common levels only, avoiding a label per height.
    values, counts = np.unique(heights, return_counts=True)
    steep_levels = np.unique(heights[ndi.maximum_filter(heights, 3) > heights + 4.0])
    for level in np.intersect1d(values[counts >= 12], steep_levels):
        equal = heights == level
        labels, _ = ndi.label(ndi.binary_dilation(equal, iterations=2))
        sizes = np.bincount(labels[equal].ravel(), minlength=labels.max() + 1)
        boxes = ndi.find_objects(labels)
        for index in np.flatnonzero(sizes[1:] >= 12) + 1:
            r, c = boxes[index - 1]
            pad = max(5, int(np.ceil(10 / cell)))
            r = slice(max(0, r.start - pad), min(heights.shape[0], r.stop + pad))
            c = slice(max(0, c.start - pad), min(heights.shape[1], c.stop + pad))
            patch = heights[r, c]
            core = (labels[r, c] == index) & equal[r, c]
            rim = ndi.binary_dilation(core, iterations=pad) & ~ndi.binary_dilation(core, iterations=pad - 2)
            if not rim.any():
                continue
            low, middle = np.percentile(patch[rim], [10, 50])
            if low - level < 4 or middle - level < 6:
                continue
            # Include bilinear mixtures of the bad plateau and valid land.
            mask = ndi.binary_propagation(core, mask=patch < low - 1.0)
            if mask[0].any() or mask[-1].any() or mask[:, 0].any() or mask[:, -1].any():
                continue
            yield r, c, mask


def repair_dropouts(heights, columns, cell, origin=(0, 0), reviewed=()):
    """Interpolate detected pits only at explicitly reviewed source locations."""
    grid = np.asarray(heights, dtype=float).reshape(-1, columns).copy()
    for r, c, mask in list(dropout_masks(grid, cell)):
        rr, cc = np.nonzero(mask)
        center = np.array([origin[0] + (c.start + cc.mean()) * cell,
                           origin[1] + (r.start + rr.mean()) * cell])
        if not any(entry.get("reason", "").strip()
                   and np.linalg.norm(center - entry["position"]) <= entry["reach_m"]
                   for entry in reviewed):
            continue
        patch = grid[r, c]
        rim = ndi.binary_dilation(mask) & ~mask
        points = np.column_stack(np.nonzero(rim))
        targets = np.column_stack(np.nonzero(mask))
        values = LinearNDInterpolator(points, patch[rim])(targets)
        if np.isfinite(values).all():
            patch[mask] = values
    return grid.ravel().tolist()
