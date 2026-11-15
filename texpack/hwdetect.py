# SpeedBreaker texture pack tools. GPL-3.0-or-later (see COPYING).
#
# How the pack builder tunes itself to the machine it runs on (the design's
# §7): which GPU upscales (a discrete one first, as the game picks; NFSMW_GPU
# overrides, by index or part of a name), the upscaler's tile size from that
# GPU's memory, how many encoders run (one per CPU thread, fewer when free
# memory is short), and when it warns (integrated graphics only: hours) or
# refuses (no Vulkan GPU, or too little disk). Pure functions of what
# speedbreaker-texpack measures, for tests/texpack_driver_test.py.
import json
import os
from dataclasses import dataclass, field

REGIONS = {
    "aebdf3c14d1e4e4ed2c130d94bc1a0e97bd6d5bf39bdfdef33c3909c9cc4ba4c": "usa",
    "aad15fc218d034de3131f746e0b763dd8049a40dd676efc87667fcaf4fc72694": "pal_es",
    "23ece29d18724ea28971d52f936964d127148bd2359ff24795da7442b9069bf4": "pal_de",
}


@dataclass
class Machine:
    gpus: list = field(default_factory=list)  # texpack-gpuinfo's objects
    threads: int = 1
    free_ram_mb: int = 0
    free_disk_mb: int = 0


@dataclass
class BuildPlan:
    gpu_index: int = -1
    gpu_name: str = ""
    tile: int = 0
    jobs_upscale: str = "1:1:2"   # realesrgan-ncnn-vulkan's -j load:proc:save
    jobs_encode: int = 1
    warn_slow: bool = False
    refuse_reason: str = None
    why: str = ""


def parse_gpuinfo(text):
    """texpack-gpuinfo's JSON line, past any driver chatter."""
    for line in reversed(text.splitlines()):
        line = line.strip()
        if line.startswith("["):
            try:
                return json.loads(line)
            except ValueError:
                return []
    return []


def pick_gpu(gpus, nfsmw_gpu=None):
    if not gpus:
        return None
    if nfsmw_gpu:
        if nfsmw_gpu.isdigit():
            for g in gpus:
                if g["index"] == int(nfsmw_gpu):
                    return g
        for g in gpus:
            if nfsmw_gpu.lower() in g["name"].lower():
                return g
    discrete = [g for g in gpus if g["discrete"]]
    return (discrete or gpus)[0]


def plan_build(m, est_pack_mb, nfsmw_gpu=None):
    p = BuildPlan()
    # The disk: the pack plus the work folder (PNGs at 4x, about the pack's size again).
    need = int(est_pack_mb * 2.5)
    if m.free_disk_mb < need:
        p.refuse_reason = f"not enough free disk: {need} MB needed, {m.free_disk_mb} MB free"
        return p
    g = pick_gpu(m.gpus, nfsmw_gpu)
    if g is None:
        p.refuse_reason = "no Vulkan GPU to run the upscaler on: only the extract step can run here"
        return p
    p.gpu_index, p.gpu_name = g["index"], g["name"]
    vram = g["vram_mb"]
    if not g["discrete"]:
        p.warn_slow = True
        vram = min(vram, 4 * 1024)  # shared memory: plan as a small card
    p.tile = 400 if vram >= 12 * 1024 else 200 if vram >= 4 * 1024 else 100
    p.jobs_upscale = "2:2:4" if vram >= 8 * 1024 else "1:1:2"
    p.jobs_encode = max(1, min(m.threads, m.free_ram_mb // 512))
    p.why = (f"{g['name']} ({'discrete' if g['discrete'] else 'integrated'}, {g['vram_mb']} MB): tile {p.tile}, "
             f"upscale threads {p.jobs_upscale}; {p.jobs_encode} encoders ({m.threads} CPU threads, {m.free_ram_mb} MB free memory)")
    return p


def todo(keys, pack_dir, force=False):
    """The keys still to build: those without a finished <key>.dds (a .partial is unfinished)."""
    if force:
        return list(keys)
    return [k for k in keys if not os.path.exists(os.path.join(pack_dir, k + ".dds"))]


def region_of(xex_sha256):
    return REGIONS.get(xex_sha256)


def prune(pack_dir, wanted_keys):
    """Removes the pack's replacements no longer wanted (a texture now classed a normal map, or
    left out by --no-ui) and unfinished .partial files; returns the names removed."""
    removed = []
    for name in os.listdir(pack_dir):
        key = name[:16]
        if name.endswith(".dds.partial") or (name.endswith(".dds") and len(name) == 20 and key not in wanted_keys):
            os.remove(os.path.join(pack_dir, name))
            removed.append(name)
    return removed
