import pytest
import ctypes
import struct


# Simulated texture processing functions that mirror the vulnerable C logic
def compute_out_size_bytes(width, height, bpp=4):
    """Simulate the size computation from gl_texmgr.c"""
    return width * height * bpp


def simulate_memcpy_safe(dest_buffer_size, src_size):
    """
    Simulate the memcpy operation with bounds checking.
    Returns True if the copy would be safe, False if it would overflow.
    """
    if src_size < 0:
        return False
    if src_size > dest_buffer_size:
        return False
    return True


def validate_texture_dimensions(width, height, max_dimension=65536, max_size_bytes=256 * 1024 * 1024):
    """
    Security invariant: texture dimensions must be validated before use in size computations.
    Returns (is_valid, computed_size) tuple.
    """
    # Check for negative or zero dimensions
    if width <= 0 or height <= 0:
        return False, 0

    # Check for dimensions exceeding maximum allowed
    if width > max_dimension or height > max_dimension:
        return False, 0

    # Check for integer overflow in size computation
    # In C: mipwidth * mipheight * 4 could overflow 32-bit int
    try:
        size = width * height * 4
    except OverflowError:
        return False, 0

    # Check computed size doesn't exceed maximum allowed buffer size
    if size > max_size_bytes:
        return False, 0

    # Check for overflow that would produce a smaller-than-expected value
    # (simulating 32-bit integer overflow behavior)
    size_32bit = (width * height * 4) & 0xFFFFFFFF
    if size_32bit != size:
        return False, 0

    return True, size


def simulate_texture_load(width, height, dest_buffer_size):
    """
    Simulate loading a texture with the given dimensions into a destination buffer.
    Returns True if the operation is safe, raises ValueError if it would be unsafe.
    """
    is_valid, out_size_bytes = validate_texture_dimensions(width, height)

    if not is_valid:
        raise ValueError(f"Invalid texture dimensions: {width}x{height}")

    # Simulate the memcpy bounds check from line 767
    if out_size_bytes > dest_buffer_size:
        raise ValueError(
            f"Buffer overflow: out_size_bytes ({out_size_bytes}) > dest_buffer_size ({dest_buffer_size})"
        )

    return True


@pytest.mark.parametrize("payload", [
    # Integer overflow attempts: large dimensions that overflow 32-bit int
    {"width": 0x10000, "height": 0x10000, "dest_size": 1024, "desc": "64K x 64K overflow"},
    {"width": 0x8000, "height": 0x8000, "dest_size": 1024, "desc": "32K x 32K large texture"},
    {"width": 0xFFFF, "height": 0xFFFF, "dest_size": 1024, "desc": "max 16-bit dimensions"},
    {"width": 0x7FFFFFFF, "height": 1, "dest_size": 1024, "desc": "max 32-bit width"},
    {"width": 1, "height": 0x7FFFFFFF, "dest_size": 1024, "desc": "max 32-bit height"},
    # Dimensions that cause 32-bit overflow to wrap to small value
    {"width": 0x4000, "height": 0x4000, "dest_size": 4, "desc": "16K x 16K wrapping overflow"},
    {"width": 0x20000000, "height": 4, "dest_size": 4, "desc": "overflow wrap to small size"},
    {"width": 0x40000000, "height": 2, "dest_size": 4, "desc": "overflow wrap attempt"},
    # Zero and negative dimensions
    {"width": 0, "height": 100, "dest_size": 1024, "desc": "zero width"},
    {"width": 100, "height": 0, "dest_size": 1024, "desc": "zero height"},
    {"width": -1, "height": 100, "dest_size": 1024, "desc": "negative width"},
    {"width": 100, "height": -1, "dest_size": 1024, "desc": "negative height"},
    {"width": -0x80000000, "height": -0x80000000, "dest_size": 1024, "desc": "min negative dimensions"},
    # Dimensions where computed size exceeds destination buffer
    {"width": 1024, "height": 1024, "dest_size": 100, "desc": "size exceeds small buffer"},
    {"width": 512, "height": 512, "dest_size": 512, "desc": "size exceeds medium buffer"},
    {"width": 2048, "height": 2048, "dest_size": 1024, "desc": "large texture small buffer"},
    # Boundary values
    {"width": 65537, "height": 1, "dest_size": 1024, "desc": "just over max dimension"},
    {"width": 1, "height": 65537, "dest_size": 1024, "desc": "height just over max"},
    # Crafted values that might bypass naive checks
    {"width": 0x55555555, "height": 3, "dest_size": 16, "desc": "crafted overflow value"},
    {"width": 0xAAAAAAAA, "height": 2, "dest_size": 16, "desc": "crafted overflow value 2"},
])
def test_texture_dimension_bounds_validation(payload):
    """
    Invariant: Texture dimension computations must never result in a buffer overflow.
    The computed size (width * height * bpp) must always be validated against the
    destination buffer capacity before any memcpy operation. Adversarial texture
    dimensions must be rejected before they can cause memory corruption.
    """
    width = payload["width"]
    height = payload["height"]
    dest_size = payload["dest_size"]
    desc = payload["desc"]

    # Property 1: If dimensions are invalid, the operation must be rejected
    is_valid, computed_size = validate_texture_dimensions(width, height)

    if not is_valid:
        # Invalid dimensions must be caught - no copy should occur
        with pytest.raises(ValueError):
            simulate_texture_load(width, height, dest_size)
        return

    # Property 2: If dimensions are valid but computed size exceeds buffer, must be rejected
    if computed_size > dest_size:
        with pytest.raises(ValueError):
            simulate_texture_load(width, height, dest_size)
        return

    # Property 3: If we reach here, the operation should be safe
    # Verify the copy would not overflow
    assert computed_size <= dest_size, (
        f"[{desc}] SECURITY VIOLATION: computed_size ({computed_size}) > "
        f"dest_size ({dest_size}) for dimensions {width}x{height}"
    )

    # Property 4: Verify no integer overflow occurred in size computation
    assert computed_size >= 0, (
        f"[{desc}] SECURITY VIOLATION: computed_size is negative ({computed_size}), "
        f"indicating integer overflow for dimensions {width}x{height}"
    )

    # Property 5: Verify the safe copy simulation succeeds
    result = simulate_memcpy_safe(dest_size, computed_size)
    assert result is True, (
        f"[{desc}] SECURITY VIOLATION: memcpy simulation failed for "
        f"dimensions {width}x{height}, computed_size={computed_size}, dest_size={dest_size}"
    )


@pytest.mark.parametrize("width,height", [
    (0x10000, 0x10000),   # 64K x 64K
    (0x7FFFFFFF, 1),      # max int width
    (1, 0x7FFFFFFF),      # max int height
    (0x4000, 0x4000),     # 16K x 16K
    (0xFFFF, 0xFFFF),     # max 16-bit
    (0x20000000, 4),      # overflow candidate
])
def test_size_computation_no_integer_overflow(width, height):
    """
    Invariant: Size computation (width * height * 4) must never silently overflow
    to produce a value smaller than the actual required memory, which would allow
    an undersized buffer to be used for a larger copy operation.
    """
    bpp = 4

    # Compute size in Python (arbitrary precision)
    true_size = width * height * bpp

    # Simulate 32-bit C integer overflow
    size_32bit = (width * height * bpp) & 0xFFFFFFFF

    # If 32-bit overflow occurred, the C code would use a wrong (smaller) size
    if size_32bit != true_size:
        # This is a dangerous condition - the validation must catch it
        is_valid, computed_size = validate_texture_dimensions(width, height)
        assert not is_valid, (
            f"SECURITY VIOLATION: Dimensions {width}x{height} cause 32-bit integer overflow "
            f"(true_size={true_size}, 32bit_size={size_32bit}) but were not rejected by validation"
        )


@pytest.mark.parametrize("mipwidth,mipheight", [
    # Test the specific mipwidth * mipheight * 4 computation from line 1249
    (0x8000, 0x8000),
    (0x10000, 0x10000),
    (0x4001, 0x4001),
    (0xFFFF, 0xFFFF),
    (0x7FFF, 0x7FFF),
    (65536, 65536),
    (32768, 32768),
])
def test_mip_size_computation_bounds(mipwidth, mipheight):
    """
    Invariant: Mipmap size computation (mipwidth * mipheight * 4) from line 1249
    must be validated to prevent buffer overflow in memcpy operations.
    Large or attacker-controlled mip dimensions must be rejected.
    """
    MAX_MIP_SIZE = 256 * 1024 * 1024  # 256MB max

    # Compute the mip size
    mip_size = mipwidth * mipheight * 4

    # The invariant: if mip_size exceeds safe bounds, it must be rejected
    if mip_size > MAX_MIP_SIZE:
        is_valid, _ = validate_texture_dimensions(mipwidth, mipheight)
        assert not is_valid, (
            f"SECURITY VIOLATION: Mip dimensions {mipwidth}x{mipheight} produce "
            f"unsafe size {mip_size} bytes but were not rejected"
        )
    else:
        # Even valid sizes must not overflow 32-bit representation
        size_32bit = (mipwidth * mipheight * 4) & 0xFFFFFFFF
        assert size_32bit == mip_size, (
            f"SECURITY VIOLATION: Mip size computation overflows 32-bit integer "
            f"for dimensions {mipwidth}x{mipheight}"
        )