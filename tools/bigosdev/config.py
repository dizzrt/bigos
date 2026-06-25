from __future__ import annotations

from . import core as _core

globals().update({name: getattr(_core, name) for name in dir(_core) if name.isupper()})
