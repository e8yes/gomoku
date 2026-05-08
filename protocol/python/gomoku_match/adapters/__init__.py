"""Adapters that bridge external player implementations to ``PlayerClient``.

Each adapter exposes a callable suitable as the ``on_turn`` parameter to
:class:`gomoku_match.PlayerClient`, plus any helper objects (engine
process wrappers, config dataclasses, etc.) the adapter needs.
"""

from .gomocup import (
    GomocupEngine,
    GomocupEngineError,
    GomocupSwap2Strategy,
    make_gomocup_callback,
)

__all__ = [
    "GomocupEngine",
    "GomocupEngineError",
    "GomocupSwap2Strategy",
    "make_gomocup_callback",
]
