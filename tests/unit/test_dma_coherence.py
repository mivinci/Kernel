"""Finite noncoherent cache model for design/09; no hardware claim.

Memory and the CPU cache are deliberately separate. Device writes bypass the
cache, and dirty eviction is an independent transition during device ownership.
This makes missing maintenance observable instead of modelling sync as a no-op.
"""
import itertools
import unittest


class Cache:
    LINE = 8

    def __init__(self):
        self.memory = bytearray([0x55] * (2 * self.LINE))
        self.lines = {}

    def load(self, address):
        index = address // self.LINE
        if index not in self.lines:
            start = index * self.LINE
            self.lines[index] = [self.memory[start:start + self.LINE], False]
        return self.lines[index]

    def read(self, address):
        return self.load(address)[0][address % self.LINE]

    def write(self, address, value):
        line = self.load(address)
        line[0][address % self.LINE] = value
        line[1] = True

    def clean(self, index):
        if index in self.lines and self.lines[index][1]:
            start = index * self.LINE
            self.memory[start:start + self.LINE] = self.lines[index][0]
            self.lines[index][1] = False

    def invalidate(self, index):
        self.lines.pop(index, None)

    def evict(self, index):
        self.clean(index)
        self.invalidate(index)

    def sync(self, offset, length, phase, direction, window=(0, 16)):
        # Validate the complete rounded range before any cache mutation.
        limit = (1 << 64) - 1
        if (phase not in (0, 1) or direction not in (1, 2, 3)
                or offset < 0 or length <= 0 or offset > limit - length):
            raise ValueError('invalid DMA range or operation')
        start = offset // self.LINE * self.LINE
        end = (offset + length + self.LINE - 1) // self.LINE * self.LINE
        if end > limit or start < window[0] or end > window[1]:
            raise ValueError('maintenance exceeds authority')
        for index in range(start // self.LINE, end // self.LINE):
            if phase == 0:
                self.clean(index)
            elif direction in (2, 3):
                self.invalidate(index)


class DmaCoherenceTests(unittest.TestCase):
    def receive(self, before, after, speculative, device_bytes=8):
        cache = Cache()
        for address in range(cache.LINE):
            cache.write(address, 0)
        if before:
            cache.sync(0, 8, 0, 2)
        if speculative:
            cache.evict(0)
            cache.read(0)  # A clean stale line may refill before completion.
        cache.memory[:device_bytes] = bytes([0xa7] * device_bytes)
        if not speculative:
            cache.evict(0)
        if after:
            cache.sync(0, 8, 1, 2)
        return bytes(cache.read(address) for address in range(device_bytes))

    def test_receive_survives_dirty_eviction_and_speculative_refill(self):
        for speculative, length in itertools.product((False, True), (1, 4, 8)):
            with self.subTest(speculative=speculative, length=length):
                self.assertEqual(self.receive(True, True, speculative, length),
                                 bytes([0xa7] * length))

    def test_omitting_before_loses_device_data_on_dirty_eviction(self):
        self.assertEqual(self.receive(False, True, False), bytes(8))

    def test_omitting_after_exposes_speculatively_cached_old_data(self):
        self.assertEqual(self.receive(True, False, True), bytes(8))

    def test_all_directions_publish_dirty_cpu_input(self):
        for direction in (1, 2, 3):
            cache = Cache()
            cache.write(0, 0x93)
            self.assertEqual(cache.memory[0], 0x55)
            cache.sync(0, 8, 0, direction)
            self.assertEqual(cache.memory[0], 0x93)
            cache.memory[0] = 0xa7
            cache.evict(0)
            self.assertEqual(cache.memory[0], 0xa7)

    def test_after_read_preserves_cpu_cache_but_write_discards_stale_line(self):
        for direction in (1, 2, 3):
            cache = Cache()
            cache.read(0)
            cache.memory[0] = 0xa7
            cache.sync(0, 8, 1, direction)
            self.assertEqual(cache.read(0), 0x55 if direction == 1 else 0xa7)

    def test_shared_line_cpu_writer_can_destroy_device_output(self):
        cache = Cache()
        cache.write(0, 0)
        cache.sync(0, 8, 0, 2)
        cache.memory[0] = 0xa7
        cache.write(7, 0x91)  # Illegal adjacent CPU-owned state in same line.
        cache.evict(0)
        self.assertEqual(cache.memory[0], 0)
        self.assertEqual(cache.memory[7], 0x91)

    def test_isolated_line_cpu_writer_preserves_device_output(self):
        cache = Cache()
        cache.write(0, 0)
        cache.sync(0, 8, 0, 2)
        cache.memory[0] = 0xa7
        cache.write(8, 0x91)
        cache.evict(1)
        cache.sync(0, 8, 1, 2)
        self.assertEqual(cache.read(0), 0xa7)
        self.assertEqual(cache.read(8), 0x91)

    def test_invalid_rounded_ranges_do_not_partially_clean(self):
        for offset, length, phase, direction, window in (
                (1, 7, 0, 2, (1, 8)), (0, 9, 0, 2, (0, 9)),
                (0, 0, 0, 2, (0, 16)), (0, 8, 2, 2, (0, 16)),
                (0, 8, 0, 4, (0, 16)),
                ((1 << 64) - 4, 8, 0, 2, (0, (1 << 64) - 1))):
            with self.subTest(offset=offset, length=length, window=window):
                cache = Cache()
                cache.write(0, 0x93)
                previous = bytes(cache.memory)
                with self.assertRaises(ValueError):
                    cache.sync(offset, length, phase, direction, window)
                self.assertEqual(bytes(cache.memory), previous)
                self.assertTrue(cache.lines[0][1])
