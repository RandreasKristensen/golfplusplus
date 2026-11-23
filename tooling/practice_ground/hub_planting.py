"""The hub's plants, grown deterministically for make_practice_ground.py.

Danish shelterbelts (laehegn): rows of mixed broadleaf a few metres deep, tall
oak and beech down the middle with birch and willow outside them, a little
spruce and a scrub of hawthorn-like bushes on the edges. A wooded crest on the
east ridge, and clipped beech hedges along the south roads. Only species of
assets/play/tuning/trees.json are used; trees keep the course world format and
hedges and edge scrub use the `bushes` format.

A plant is (x, z, radius) once placed; `blocked(x, z, radius)` says whether
something that must stay clear is in the way.
"""
from __future__ import annotations
import math
import random

TREE_SHARES = {"inner": (("oak", 4), ("beech", 4), ("spruce", 1)), "outer": (("birch", 4), ("willow", 1), ("beech", 1), ("oak", 1))}
TREE_HEIGHTS = {"oak": (12., 17.), "beech": (12., 16.), "spruce": (10., 15.), "birch": (8., 12.), "willow": (7., 10.)}
BUSH_RADIUS, BUSH_HEIGHT = (1.0, 1.6), (1.4, 2.1)
HEDGE_RADIUS, HEDGE_HEIGHT, HEDGE_STEP = 1.2, 1.7, 1.3


def tree(rng, x, z, kind):
    """A tree in the course world format: crown and trunk in the proportions of the imported courses' trees."""
    species = rng.choices([s for s, _ in TREE_SHARES[kind]], [w for _, w in TREE_SHARES[kind]])[0]
    height = rng.uniform(*TREE_HEIGHTS[species])
    return {"position": [round(x, 2), 0., round(z, 2)], "trunk_radius": round(.038 * height, 2),
            "trunk_height": round(.45 * height, 2), "leaf_radius": round(.275 * height, 2),
            "leaf_height": round(.55 * height, 2), "species": species}


def bush(x, z, radius, height, species=None):
    """A bush: scrub unless `species` is named."""
    planted = {"position": [round(x, 2), 0., round(z, 2)], "radius": round(radius, 2), "height": round(height, 2)}
    if species:
        planted["species"] = species
    return planted


def shelterbelt(rng, centre, start, end, blocked, trees, bushes, step=4.):
    """A belt along the line start-end: three rows, tall in the middle, scrub on both edges.
    `centre(t)` is the sideways wobble of the belt's middle at the fraction t of its length."""
    length = math.dist(start, end)
    along = ((end[0] - start[0]) / length, (end[1] - start[1]) / length)
    side = (-along[1], along[0])
    for row, offset in enumerate((-3.4, 0., 3.4)):
        kind = "inner" if row == 1 else "outer"
        position = rng.uniform(0., step) + (step / 2 if row == 1 else 0.)
        while position < length:
            sideways = centre(position / length) + offset + rng.uniform(-.7, .7)
            x = start[0] + along[0] * position + side[0] * sideways
            z = start[1] + along[1] * position + side[1] * sideways
            plant = tree(rng, x, z, kind)
            if not blocked(x, z, plant["leaf_radius"]):
                trees.append(plant)
            position += step + rng.uniform(-.8, .8)
    for sign in (-1., 1.):
        position = rng.uniform(0., 3.)
        while position < length:
            sideways = centre(position / length) + sign * rng.uniform(5.4, 6.2)
            x = start[0] + along[0] * position + side[0] * sideways
            z = start[1] + along[1] * position + side[1] * sideways
            radius = rng.uniform(*BUSH_RADIUS)
            if rng.random() < .75 and not blocked(x, z, radius):
                bushes.append(bush(x, z, radius, rng.uniform(*BUSH_HEIGHT)))
            position += rng.uniform(2.2, 3.6)


def copse_noise(x, z):
    return math.sin(x * .043 + 1.3) * math.sin(z * .031) + .5 * math.sin((x + z) * .07)


def crest_woods(rng, bounds, wanted, blocked, trees, spacing=9.):
    """Copses of tall trees on a jittered grid inside bounds (x0, z0, x1, z1), where `wanted(x, z)` and the noise agree."""
    x = bounds[0]
    while x < bounds[2]:
        z = bounds[1]
        while z < bounds[3]:
            px, pz = x + rng.uniform(-3., 3.), z + rng.uniform(-3., 3.)
            if wanted(px, pz) and copse_noise(px, pz) > -.2:
                plant = tree(rng, px, pz, "inner" if rng.random() < .7 else "outer")
                if not blocked(px, pz, plant["leaf_radius"]):
                    trees.append(plant)
            z += spacing
        x += spacing


def hedge(rng, start, end, blocked, bushes):
    """A clipped beech hedge along start-end: one bush every HEDGE_STEP, none where something must stay clear.
    Clipped beech keeps most of its leaves withered through winter (the `hedge` species)."""
    length = math.dist(start, end)
    for k in range(int(length / HEDGE_STEP) + 1):
        t = k * HEDGE_STEP / length
        x, z = start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t
        if not blocked(x, z, HEDGE_RADIUS):
            bushes.append(bush(x, z, HEDGE_RADIUS + rng.uniform(-.1, .15), HEDGE_HEIGHT + rng.uniform(-.1, .2), "hedge"))
