"""Regression checks for texture minification and repeat addressing."""
import importlib.util
from pathlib import Path
import unittest
import numpy as np

spec = importlib.util.spec_from_file_location("baker", Path(__file__).parents[1] / "tools/prepare_terrain_detail.py")
baker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baker)


class TerrainFilteringTests(unittest.TestCase):
    def test_minification_removes_phase_dependent_aliasing(self):
        # Eight source pixels per bake pixel, matching 512px grass repeated
        # sixteen times into a 1024px tile. Direct bilinear sampling can
        # incorrectly produce solid black/white depending on UV phase.
        pattern = np.tile([0, 0, 0, 0, 1, 1, 1, 1], (512, 64)).astype(np.float32)
        image = np.repeat(pattern[:, :, None], 3, axis=2)
        filtered = baker.prefilter(image, 8, 8)
        self.assertEqual(filtered.shape, (64, 64, 3))
        for phase in (0, .001, .003, .005):
            u, v = np.meshgrid((np.arange(64) + .5) / 64 + phase, (np.arange(64) + .5) / 64)
            np.testing.assert_allclose(baker.sample(filtered, u, v, True), .5, atol=1e-6)

    def test_magnification_preserves_source_and_repeat_seam(self):
        image = np.array([[[0., 0., 0.], [1., 1., 1.]]], dtype=np.float32)
        self.assertIs(baker.prefilter(image, .25, .25), image)
        u = np.array([[0., 1., -1.]], dtype=np.float32)
        np.testing.assert_allclose(baker.sample(image, u, u * 0, True), .5)
        np.testing.assert_allclose(baker.sample(image, u, u * 0, False)[0, :, 0], [0, 1, 0])

    def test_filter_preserves_large_scale_color_and_anisotropic_size(self):
        image = np.zeros((64, 64, 3), dtype=np.float32)
        image[:, 32:] = 1
        filtered = baker.prefilter(image, 8, 2)
        self.assertEqual(filtered.shape, (32, 8, 3))
        np.testing.assert_array_equal(filtered[:, :4], 0)
        np.testing.assert_array_equal(filtered[:, 4:], 1)


if __name__ == "__main__":
    unittest.main()
