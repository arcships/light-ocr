"""Exercise the shipped C ABI on a host without an AMD NPU."""
import ctypes as c
import os
from pathlib import Path
import unittest

PACKAGE = os.environ.get('LIGHT_OCR_AMD_TEST_PACKAGE')
Create = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_size_t, c.POINTER(c.c_void_p), c.c_void_p, c.c_size_t)
Load = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p, c.c_size_t, c.c_uint32, c.POINTER(c.c_void_p), c.c_void_p, c.c_size_t)
Run = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p, c.c_size_t, c.c_void_p, c.c_size_t)
Destroy = c.CFUNCTYPE(None, c.c_void_p)
class Api(c.Structure):
    _fields_ = [('version', c.c_uint32), ('size', c.c_uint32), ('create', Create),
                ('load', Load), ('run', Run), ('destroy_session', Destroy), ('destroy_engine', Destroy)]

@unittest.skipUnless(PACKAGE, 'requires generated AMD package, not an AMD device')
class AmdAieRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(PACKAGE)
        cls.library = c.CDLL(str(cls.root/'native/amdnpu/lib/liblight_ocr_amdaie.so.1'))
        cls.library.LightOcrAieGetApi.argtypes = [c.c_uint32]
        cls.library.LightOcrAieGetApi.restype = c.POINTER(Api)
        cls.api = cls.library.LightOcrAieGetApi(1).contents

    def test_versioned_abi(self):
        self.assertFalse(self.library.LightOcrAieGetApi(0))
        self.assertFalse(self.library.LightOcrAieGetApi(2))
        self.assertEqual(self.api.version, 1)
        self.assertEqual(self.api.size, c.sizeof(Api))

    def test_null_input_errors_and_null_destruction(self):
        error = c.create_string_buffer(256)
        handle = c.c_void_p(123)
        self.assertNotEqual(self.api.create(None, 0, c.byref(handle), error, len(error)), 0)
        self.assertIsNone(handle.value)
        self.assertIn(b'Missing parameter archive', error.value)
        handle.value = 123
        self.assertNotEqual(self.api.load(None, None, 0, 320, c.byref(handle), error, len(error)), 0)
        self.assertIsNone(handle.value)
        self.assertIn(b'Invalid recognition module', error.value)
        self.assertNotEqual(self.api.run(None, None, 0, None, 0, error, len(error)), 0)
        self.assertIn(b'tensor dimensions', error.value)
        self.api.destroy_session(None)
        self.api.destroy_engine(None)

    def test_error_buffer_truncation(self):
        handle = c.c_void_p()
        error = c.create_string_buffer(1)
        self.assertNotEqual(self.api.create(None, 0, c.byref(handle), error, 1), 0)
        self.assertEqual(error.raw, b'\0')
        self.assertNotEqual(self.api.create(None, 0, c.byref(handle), None, 0), 0)

    def test_real_weights_without_device_fail_cleanly(self):
        weights = c.create_string_buffer((self.root/'native/amdnpu/models/recognition.irpa').read_bytes())
        for _ in range(3):
            handle = c.c_void_p()
            error = c.create_string_buffer(2048)
            status = self.api.create(weights, len(weights)-1, c.byref(handle), error, len(error))
            if not status:
                self.api.destroy_engine(handle)
                self.fail('this test requires a host without AMD NPU')
            self.assertIsNone(handle.value)
            self.assertTrue(error.value)
            self.assertRegex(error.value.decode(), r'(?i)(amdxdna|device|accel|driver)')

if __name__ == '__main__': unittest.main()
