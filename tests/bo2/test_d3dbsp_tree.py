"""The BSP writer's collision k-d tree (tools/bo2/d3dbsp.py): every box must reach a leaf.

    python3 -m unittest discover -s tests/bo2 -v
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "bo2"))
import d3dbsp  # noqa: E402


def build(boxes):
    mins = [min(b[0][k] for b in boxes) for k in range(3)]
    maxs = [max(b[1][k] for b in boxes) for k in range(3)]
    return d3dbsp._build_tree(boxes, mins, maxs, lambda normal, dist: 0)


class TreeTest(unittest.TestCase):
    def test_flat_boxes_on_a_split_plane(self):
        # A level floor of flat triangles at z = -64 over tall boxes: the median split along z lands on -64 itself.
        floor = [([x, y, -64.0], [x + 50.0, y + 50.0, -64.0]) for x in range(0, 500, 50) for y in range(0, 100, 50)]
        walls = [([x, 0.0, -400.0], [x + 10.0, 10.0, 200.0]) for x in range(0, 500, 25)]
        boxes = floor + walls
        nodes, leafs = build(boxes)
        self.assertTrue(nodes)
        reached = {i for leaf in leafs for i in leaf}
        self.assertEqual(sorted(set(range(len(boxes))) - reached), [])

    def test_many_boxes_all_reached(self):
        boxes = [([x, y, z], [x + 30.0, y + 30.0, z + (0.0 if (x + y) % 64 else 20.0)])
                 for x in range(0, 640, 32) for y in range(0, 640, 32) for z in (0.0, 32.0)]
        _, leafs = build(boxes)
        reached = {i for leaf in leafs for i in leaf}
        self.assertEqual(len(reached), len(boxes))


class AabbTreeTest(unittest.TestCase):
    """The leaf's collision AABB trees, walked as CM_TraceThroughAabbTree does: a stack of 128 nodes, every child
    the trace touches pushed (here a trace touching everything, the worst case)."""

    @staticmethod
    def partitions(count, material):
        return [([x * 10.0, 0.0, -64.0], [x * 10.0 + 12.0, 12.0, -64.0], material, [x]) for x in range(count)]

    def walk(self, nodes, root):
        stack, deepest, reached = [root], 1, []
        while stack:
            lo, hi, material, count, index = nodes[stack.pop()]
            if count:
                stack.extend(range(index, index + count))
                deepest = max(deepest, len(stack))
            else:
                reached.append(index)
        return deepest, reached

    def test_every_partition_reached_and_the_stack_bounded(self):
        parts = self.partitions(607, 3) + self.partitions(5, 7)
        roots = [(3, list(range(607))), (7, list(range(607, 612))), (3, [0])]
        nodes = d3dbsp._collision_aabb_tree(roots, parts)
        for r, (material, ids) in enumerate(roots):
            deepest, reached = self.walk(nodes, r)
            self.assertEqual(sorted(reached), sorted(ids))
            self.assertLess(deepest, 128)
            self.assertGreater(nodes[r][3], 0, "a root always has children")
        for lo, hi, material, count, index in nodes:
            if count:
                self.assertLessEqual(count, d3dbsp.AABB_TREE_FANOUT)

    def test_a_node_covers_its_children(self):
        parts = self.partitions(100, 1)
        nodes = d3dbsp._collision_aabb_tree([(1, list(range(100)))], parts)
        for lo, hi, material, count, index in nodes:
            for c in range(index, index + count):
                for k in range(3):
                    self.assertLessEqual(lo[k], nodes[c][0][k])
                    self.assertGreaterEqual(hi[k], nodes[c][1][k])


if __name__ == "__main__":
    unittest.main()
