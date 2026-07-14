#!/usr/bin/env python3

import unittest

from compare import box_metrics, polygon_area, polygon_clip
from run_quality import detection_match


class PolygonComparisonTests(unittest.TestCase):
    def test_area_supports_empty_triangle_and_hexagon(self) -> None:
        self.assertEqual(polygon_area([]), 0.0)
        self.assertEqual(polygon_area([[0, 0], [2, 0], [0, 2]]), 2.0)
        self.assertEqual(
            polygon_area([[0, 0], [2, 0], [3, 1], [2, 2], [0, 2], [-1, 1]]),
            6.0,
        )

    def test_rotated_quad_intersection_may_have_more_than_four_vertices(self) -> None:
        square = [[0, 0], [4, 0], [4, 4], [0, 4]]
        diamond = [[2, -1], [5, 2], [2, 5], [-1, 2]]
        intersection = polygon_clip(square, diamond)
        self.assertGreater(len(intersection), 4)
        self.assertGreater(polygon_area(intersection), 0.0)
        iou, corner = box_metrics(square, square)
        self.assertEqual(iou, 1.0)
        self.assertEqual(corner, 0.0)


class QualityMatchingTests(unittest.TestCase):
    def test_detection_matching_is_one_to_one_and_counts_unmatched_boxes(self) -> None:
        expected = [
            [[0, 0], [10, 0], [10, 10], [0, 10]],
            [[20, 0], [30, 0], [30, 10], [20, 10]],
        ]
        predicted = [
            [[0, 0], [10, 0], [10, 10], [0, 10]],
            [[1, 0], [11, 0], [11, 10], [1, 10]],
            [[20, 0], [30, 0], [30, 10], [20, 10]],
        ]
        result = detection_match(predicted, expected)
        self.assertEqual(result["truePositives"], 2)
        self.assertEqual(result["falsePositives"], 1)
        self.assertEqual(result["falseNegatives"], 0)
        self.assertAlmostEqual(result["precision"], 2 / 3)
        self.assertEqual(result["recall"], 1.0)

    def test_detection_matching_handles_blank_and_missed_reference(self) -> None:
        self.assertEqual(detection_match([], [
            [[0, 0], [10, 0], [10, 10], [0, 10]]
        ])["hmean"], 0.0)
        self.assertEqual(detection_match([], [])["hmean"], 1.0)


if __name__ == "__main__":
    unittest.main()
