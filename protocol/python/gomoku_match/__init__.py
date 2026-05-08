"""Project-neutral Gomoku Swap2 matchmaking server and protocol.

The match server owns the canonical game state and acts as referee.
Player engines and GUI observers connect as clients and follow the
documented protocol (see ``docs/protocol_v2.md`` for the wire format,
``spec.md`` for the developer guide). This package has no dependency on
any specific Gomoku engine — it is intended as a standard that any
engine can adopt.
"""

from .board import (
    Action,
    Board,
    BoardConfig,
    GamePhase,
    GameResult,
    IllegalActionError,
    Player,
    Stone,
)
from .elo import (
    DEFAULT_INITIAL_RATING,
    EloEngine,
    MatchRatingRow,
    RatingRow,
    expected_score,
    k_factor_for,
)
from .observer_client import ObserverClient
from .persistence import MatchRecord, MatchStore, MoveRecord
from .player_client import PlayerClient
from .protocol import (
    ErrorCode,
    Event,
    ProtocolError,
    Request,
    Response,
    decode_message,
    encode_message,
)
from .server import MatchServer, MatchSettings, ServerError
from .tcp_transport import TcpListener, TcpTransport, connect_tcp, parse_listen_url
from .transports import InProcessTransport, Transport

PROTOCOL_VERSION = "2.0"

__all__ = [
    "Action",
    "Board",
    "BoardConfig",
    "DEFAULT_INITIAL_RATING",
    "EloEngine",
    "ErrorCode",
    "Event",
    "GamePhase",
    "GameResult",
    "IllegalActionError",
    "InProcessTransport",
    "MatchRecord",
    "MatchRatingRow",
    "MatchServer",
    "MatchSettings",
    "MatchStore",
    "MoveRecord",
    "ObserverClient",
    "PROTOCOL_VERSION",
    "Player",
    "PlayerClient",
    "ProtocolError",
    "RatingRow",
    "Request",
    "Response",
    "ServerError",
    "Stone",
    "TcpListener",
    "TcpTransport",
    "Transport",
    "connect_tcp",
    "decode_message",
    "encode_message",
    "expected_score",
    "k_factor_for",
    "parse_listen_url",
]
