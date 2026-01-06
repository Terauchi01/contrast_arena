#!/usr/bin/env python3
"""Simple Python client that plays random legal moves over the contrast_arena protocol."""
from __future__ import annotations

import argparse
import random
import re
import socket
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

BOARD_W = 5
BOARD_H = 5
ORTHO_DIRS = [(1, 0), (-1, 0), (0, 1), (0, -1)]
DIAG_DIRS = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
ALL_DIRS = ORTHO_DIRS + DIAG_DIRS


@dataclass
class StateSnapshot:
    turn: str = "X"
    status: str = "ongoing"
    last_move: str = ""
    pieces: Dict[str, str] = None
    tiles: Dict[str, str] = None
    stock_black: Dict[str, int] = None
    stock_gray: Dict[str, int] = None

    def __post_init__(self) -> None:
        self.pieces = self.pieces or {}
        self.tiles = self.tiles or {}
        self.stock_black = self.stock_black or {}
        self.stock_gray = self.stock_gray or {}


def coord_to_xy(coord: str) -> Tuple[int, int]:
    x = ord(coord[0]) - ord("a")
    rank = ord(coord[1]) - ord("1")
    y = BOARD_H - 1 - rank
    return x, y


def xy_to_coord(x: int, y: int) -> str:
    file_char = chr(ord("a") + x)
    rank_char = chr(ord("1") + (BOARD_H - 1 - y))
    return f"{file_char}{rank_char}"


def parse_entries(text: str) -> Dict[str, str]:
    result: Dict[str, str] = {}
    if not text:
        return result
    for item in text.split(","):
        if not item:
            continue
        if ":" not in item:
            continue
        coord, value = item.split(":", 1)
        result[coord.strip()] = value.strip()
    return result


def parse_counts(text: str) -> Dict[str, int]:
    result: Dict[str, int] = {}
    if not text:
        return result
    for item in text.split(","):
        if not item or ":" not in item:
            continue
        key, value = item.split(":", 1)
        try:
            result[key.strip().upper()] = int(value.strip())
        except ValueError:
            continue
    return result


def parse_state_block(lines: List[str]) -> StateSnapshot:
    data: Dict[str, str] = {}
    for line in lines:
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key.strip()] = value.strip()
    snapshot = StateSnapshot()
    if "turn" in data and data["turn"]:
        snapshot.turn = data["turn"][0].upper()
    if "status" in data:
        snapshot.status = data["status"]
    if "last" in data:
        snapshot.last_move = data["last"]
    if "pieces" in data:
        snapshot.pieces = {k: v.upper() for k, v in parse_entries(data["pieces"]).items()}
    if "tiles" in data:
        snapshot.tiles = {k: v.lower() for k, v in parse_entries(data["tiles"]).items()}
    if "stock_b" in data:
        snapshot.stock_black = parse_counts(data["stock_b"])
    if "stock_g" in data:
        snapshot.stock_gray = parse_counts(data["stock_g"])
    return snapshot


def build_board(snapshot: StateSnapshot) -> List[List[Dict[str, Optional[str]]]]:
    board = [[{"occupant": None, "tile": None} for _ in range(BOARD_W)] for _ in range(BOARD_H)]
    for coord, symbol in snapshot.pieces.items():
        x, y = coord_to_xy(coord)
        board[y][x]["occupant"] = symbol.upper()
    for coord, tile in snapshot.tiles.items():
        x, y = coord_to_xy(coord)
        board[y][x]["tile"] = tile.lower()
    return board


def collect_empty_cells(board: List[List[Dict[str, Optional[str]]]]) -> List[Tuple[int, int]]:
    empties: List[Tuple[int, int]] = []
    for y in range(BOARD_H):
        for x in range(BOARD_W):
            cell = board[y][x]
            if cell["occupant"] is None and cell["tile"] is None:
                empties.append((x, y))
    return empties


def compute_legal_moves(
    board: List[List[Dict[str, Optional[str]]]],
    player: str,
    inv_black: int,
    inv_gray: int,
) -> List[Dict[str, Optional[Tuple[str, int, int]]]]:
    base_moves: List[Tuple[int, int, int, int]] = []
    for y in range(BOARD_H):
        for x in range(BOARD_W):
            cell = board[y][x]
            if cell["occupant"] != player:
                continue
            tile = cell["tile"]
            if tile is None:
                dirs = ORTHO_DIRS
            elif tile == "b":
                dirs = DIAG_DIRS
            else:
                dirs = ALL_DIRS
            for dx, dy in dirs:
                tx = x + dx
                ty = y + dy
                if not (0 <= tx < BOARD_W and 0 <= ty < BOARD_H):
                    continue
                target_occ = board[ty][tx]["occupant"]
                if target_occ is not None and target_occ != player:
                    continue
                if target_occ is None:
                    base_moves.append((x, y, tx, ty))
                else:
                    jx, jy = tx, ty
                    while 0 <= jx < BOARD_W and 0 <= jy < BOARD_H and board[jy][jx]["occupant"] == player:
                        jx += dx
                        jy += dy
                    if 0 <= jx < BOARD_W and 0 <= jy < BOARD_H and board[jy][jx]["occupant"] is None:
                        base_moves.append((x, y, jx, jy))
    empties = collect_empty_cells(board)
    moves: List[Dict[str, Optional[Tuple[str, int, int]]]] = []
    for base in base_moves:
        moves.append({"base": base, "tile": None})
        if inv_black > 0:
            for tx, ty in empties:
                moves.append({"base": base, "tile": ("b", tx, ty)})
        if inv_gray > 0:
            for tx, ty in empties:
                moves.append({"base": base, "tile": ("g", tx, ty)})
    return moves


class PythonRandomBot:
    def __init__(self, host: str, port: int, role: str, name: str, model: str) -> None:
        self.host = host
        self.port = port
        self.role_request = role.upper()
        self.nickname = name
        self.model = model
        self.socket: Optional[socket.socket] = None
        self.buffer = ""
        self.collecting_state = False
        self.state_lines: List[str] = []
        self.player_role: Optional[str] = None
        self.awaiting_response = False
        self.last_status: Optional[str] = None

    def connect(self) -> None:
        self.socket = socket.create_connection((self.host, self.port))
        print(f"Connected to {self.host}:{self.port}")
        self.send_role_request()

    def send_role_request(self) -> None:
        role = self.role_request or "-"
        name = self.nickname or "-"
        model = self.model or "python_random"
        self.send_line(f"ROLE {role} {name} {model}\n")

    def send_line(self, payload: str) -> None:
        if not self.socket:
            return
        try:
            self.socket.sendall(payload.encode("ascii"))
        except OSError as exc:
            print(f"[WARN] send failed: {exc}")

    def run(self) -> None:
        if not self.socket:
            raise RuntimeError("call connect() first")
        try:
            while True:
                chunk = self.socket.recv(4096)
                if not chunk:
                    print("[INFO] Server closed connection")
                    break
                self.buffer += chunk.decode("utf-8", errors="ignore")
                self.process_buffer()
        except KeyboardInterrupt:
            print("\nInterrupted, closing ...")
        finally:
            if self.socket:
                self.socket.close()

    def process_buffer(self) -> None:
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            line = line.rstrip("\r")
            if self.collecting_state:
                if line == "END":
                    snapshot = parse_state_block(self.state_lines)
                    self.state_lines.clear()
                    self.collecting_state = False
                    self.handle_snapshot(snapshot)
                else:
                    self.state_lines.append(line)
                continue
            if line == "STATE":
                self.collecting_state = True
                self.state_lines.clear()
                continue
            self.handle_line(line)

    def handle_line(self, line: str) -> None:
        if line.startswith("INFO "):
            print(line)
            match = re.search(r"You are\s+([XO])", line)
            if match:
                self.player_role = match.group(1)
        elif line.startswith("ERROR "):
            print(line, file=sys.stderr)
            self.awaiting_response = False
        elif line:
            print(line)

    def handle_snapshot(self, snapshot: StateSnapshot) -> None:
        self.awaiting_response = False
        self.last_status = snapshot.status
        if snapshot.status != "ongoing":
            print(f"[RESULT] {snapshot.status}")
            return
        if not self.player_role:
            self.player_role = snapshot.turn.upper()
        if snapshot.turn.upper() != (self.player_role or ""):
            return
        self.try_play_random(snapshot)

    def try_play_random(self, snapshot: StateSnapshot) -> None:
        board = build_board(snapshot)
        role = (self.player_role or "X").upper()
        inv_black = snapshot.stock_black.get(role, 0)
        inv_gray = snapshot.stock_gray.get(role, 0)
        moves = compute_legal_moves(board, role, inv_black, inv_gray)
        if not moves:
            print("[WARN] No legal moves available")
            return
        move = random.choice(moves)
        sx, sy, dx, dy = move["base"]
        origin = xy_to_coord(sx, sy)
        target = xy_to_coord(dx, dy)
        if move["tile"] is None:
            tile_text = "-1"
        else:
            color, tx, ty = move["tile"]
            tile_text = f"{xy_to_coord(tx, ty)}{color}"
        move_text = f"{origin},{target} {tile_text}"
        print(f"[AUTO] Sending {move_text}")
        self.send_line(f"MOVE {move_text}\n")
        self.awaiting_response = True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Python random bot for contrast_arena")
    parser.add_argument("--host", default="127.0.0.1", help="接続先ホスト名 (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8765, help="接続ポート (default: 8765)")
    parser.add_argument("--role", default="-", help="希望ロール (X/O/spectator/-)")
    parser.add_argument("--name", default="PyRandom", help="ニックネーム")
    parser.add_argument("--model", default="python_random", help="サーバへ伝えるモデル名")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    bot = PythonRandomBot(args.host, args.port, args.role, args.name, args.model)
    bot.connect()
    bot.run()


if __name__ == "__main__":
    main()
