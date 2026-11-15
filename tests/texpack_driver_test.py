# SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
#
# The texture pack builder's hardware tuning (texpack/hwdetect.py): which GPU
# upscales, its tile size and parallelism, how many encoders run, when it
# warns or refuses; and the build's resume rule (a finished <key>.dds is
# skipped, a stray .partial is redone). Pure functions, no GPU needed.
#
# Run: python3 -B tests/texpack_driver_test.py
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "texpack"))
import hwdetect  # noqa: E402

GiB = 1024


def machine(gpus, threads=24, ram=32 * GiB, disk=100 * GiB):
    return hwdetect.Machine(gpus=gpus, threads=threads, free_ram_mb=ram, free_disk_mb=disk)


def gpu(name, discrete, vram, index=0):
    return {"index": index, "name": name, "discrete": discrete, "vram_mb": vram, "bc7": True}


class PlanBuild(unittest.TestCase):
    def test_big_discrete_gpu(self):
        p = hwdetect.plan_build(machine([gpu("RX 9070 XT", True, 16 * GiB)]), 3000)
        self.assertEqual((p.gpu_index, p.tile), (0, 400))
        self.assertFalse(p.warn_slow)
        self.assertIsNone(p.refuse_reason)

    def test_mid_discrete_gpu(self):
        self.assertEqual(hwdetect.plan_build(machine([gpu("GTX 1060", True, 6 * GiB)]), 3000).tile, 200)

    def test_small_discrete_gpu(self):
        self.assertEqual(hwdetect.plan_build(machine([gpu("GT 1030", True, 2 * GiB)]), 3000).tile, 100)

    def test_discrete_preferred_over_integrated(self):
        m = machine([gpu("Radeon 610M", False, 20 * GiB, 0), gpu("RTX 3060", True, 12 * GiB, 1)])
        self.assertEqual(hwdetect.plan_build(m, 3000).gpu_index, 1)

    def test_integrated_only_warns(self):
        p = hwdetect.plan_build(machine([gpu("Steam Deck", False, 8 * GiB)]), 3000)
        self.assertTrue(p.warn_slow)
        self.assertIsNone(p.refuse_reason)

    def test_no_gpu_refuses_with_extract_only(self):
        p = hwdetect.plan_build(machine([]), 3000)
        self.assertIn("extract", p.refuse_reason)

    def test_short_disk_refuses(self):
        p = hwdetect.plan_build(machine([gpu("RX 9070 XT", True, 16 * GiB)], disk=1 * GiB), 3000)
        self.assertIn("disk", p.refuse_reason)

    def test_encoders_capped_by_ram(self):
        p = hwdetect.plan_build(machine([gpu("RX 9070 XT", True, 16 * GiB)], threads=24, ram=4 * GiB), 3000)
        self.assertLessEqual(p.jobs_encode, 4 * GiB // 512)
        self.assertGreaterEqual(p.jobs_encode, 1)

    def test_encoders_one_per_thread(self):
        self.assertEqual(hwdetect.plan_build(machine([gpu("RX 9070 XT", True, 16 * GiB)], threads=24), 3000).jobs_encode, 24)

    def test_nfsmw_gpu_picks_by_name_or_index(self):
        m = machine([gpu("RX 9070 XT", True, 16 * GiB, 0), gpu("Radeon 610M", False, 20 * GiB, 1)])
        self.assertEqual(hwdetect.plan_build(m, 3000, nfsmw_gpu="610M").gpu_index, 1)
        self.assertEqual(hwdetect.plan_build(m, 3000, nfsmw_gpu="1").gpu_index, 1)
        self.assertEqual(hwdetect.plan_build(m, 3000, nfsmw_gpu="nothing").gpu_index, 0)

    def test_parse_gpuinfo_ignores_driver_chatter(self):
        out = 'WARNING: radv is not a conformant Vulkan implementation\n[{"index": 0, "name": "X", "discrete": true, "vram_mb": 8, "bc7": true}]\n'
        self.assertEqual(hwdetect.parse_gpuinfo(out)[0]["name"], "X")
        self.assertEqual(hwdetect.parse_gpuinfo("garbage"), [])


class Resume(unittest.TestCase):
    def test_atomic_resume(self):
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "aaaaaaaaaaaaaaaa.dds"), "wb").write(b"DDS ")
            open(os.path.join(d, "bbbbbbbbbbbbbbbb.dds.partial"), "wb").write(b"DDS ")
            keys = ["aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb", "cccccccccccccccc"]
            self.assertEqual(hwdetect.todo(keys, d, force=False), ["bbbbbbbbbbbbbbbb", "cccccccccccccccc"])
            self.assertEqual(hwdetect.todo(keys, d, force=True), keys)

    def test_stale_files_pruned(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("aaaaaaaaaaaaaaaa.dds", "bbbbbbbbbbbbbbbb.dds", "cccccccccccccccc.dds.partial", "pack.json"):
                open(os.path.join(d, name), "wb").write(b"x")
            removed = hwdetect.prune(d, {"aaaaaaaaaaaaaaaa"})
            self.assertEqual(sorted(removed), ["bbbbbbbbbbbbbbbb.dds", "cccccccccccccccc.dds.partial"])
            self.assertEqual(sorted(os.listdir(d)), ["aaaaaaaaaaaaaaaa.dds", "pack.json"])

    def test_region_from_xex(self):
        self.assertEqual(hwdetect.region_of("aebdf3c14d1e4e4ed2c130d94bc1a0e97bd6d5bf39bdfdef33c3909c9cc4ba4c"), "usa")
        self.assertEqual(hwdetect.region_of("aad15fc218d034de3131f746e0b763dd8049a40dd676efc87667fcaf4fc72694"), "pal_es")
        self.assertEqual(hwdetect.region_of("23ece29d18724ea28971d52f936964d127148bd2359ff24795da7442b9069bf4"), "pal_de")
        self.assertIsNone(hwdetect.region_of("00"))


if __name__ == "__main__":
    unittest.main()
