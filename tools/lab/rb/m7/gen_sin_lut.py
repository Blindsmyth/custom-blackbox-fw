#!/usr/bin/env python3
"""Emit 257-entry full-cycle sin LUT for USE_FAST_PHASOR."""
import math
print(",\n".join(f"    {math.sin(2 * math.pi * i / 256):.8f}f" for i in range(257)))
