#!/usr/bin/env python3
"""AlphaZero client for contrast_arena protocol."""
import argparse
import socket
import sys
from collections import deque
from pathlib import Path

# AlphaZero modules
sys.path.insert(0, str(Path(__file__).parent.parent / "ai" / "contrast_alphazero"))
import numpy as np
import torch
from config import training_config
from contrast_game import ContrastGame, P1, P2, TILE_BLACK, TILE_GRAY, TILE_WHITE
from model import ContrastDualPolicyNet
from mcts import MCTS

BOARD_SIZE = 5
HISTORY_SIZE = 8


def coord_to_xy(coord):
    """Protocol coord 'a1' -> (x, protocol_y)"""
    x = ord(coord[0]) - ord('a')
    protocol_y = BOARD_SIZE - 1 - (ord(coord[1]) - ord('1'))
    return x, protocol_y


def xy_to_coord(x, protocol_y):
    """(x, protocol_y) -> Protocol coord 'a1'"""
    file_char = chr(ord('a') + x)
    rank_char = chr(ord('1') + (BOARD_SIZE - 1 - protocol_y))
    return f"{file_char}{rank_char}"


def parse_state_block(lines):
    """Parse STATE block"""
    data = {}
    for line in lines:
        if '=' not in line:
            continue
        key, value = line.split('=', 1)
        data[key.strip()] = value.strip()
    
    turn = data.get('turn', 'X')[0].upper()
    status = data.get('status', 'ongoing')
    pieces = {}
    tiles = {}
    stock_black = {}
    stock_gray = {}
    
    if 'pieces' in data:
        for item in data['pieces'].split(','):
            if ':' in item:
                coord, piece = item.split(':', 1)
                pieces[coord.strip()] = piece.strip().upper()
    
    if 'tiles' in data:
        for item in data['tiles'].split(','):
            if ':' in item:
                coord, tile = item.split(':', 1)
                tiles[coord.strip()] = tile.strip().lower()
    
    if 'stock_b' in data:
        for item in data['stock_b'].split(','):
            if ':' in item:
                player, count = item.split(':', 1)
                stock_black[player.strip().upper()] = int(count.strip())
    
    if 'stock_g' in data:
        for item in data['stock_g'].split(','):
            if ':' in item:
                player, count = item.split(':', 1)
                stock_gray[player.strip().upper()] = int(count.strip())
    
    return turn, status, pieces, tiles, stock_black, stock_gray


def snapshot_to_game(turn, pieces, tiles, stock_black, stock_gray):
    """Convert protocol state to ContrastGame
    
    Coordinate conversion:
    - Protocol: X at y=0 (top), O at y=4 (bottom)
    - AlphaZero: P1 at y=4 (bottom), P2 at y=0 (top)
    """
    game = ContrastGame()
    game.pieces.fill(0)
    game.tiles.fill(TILE_WHITE)
    
    # Place pieces (with y-axis flip)
    for coord, piece in pieces.items():
        x, protocol_y = coord_to_xy(coord)
        alphazero_y = BOARD_SIZE - 1 - protocol_y
        game.pieces[alphazero_y, x] = P1 if piece == 'X' else P2
    
    # Place tiles (with y-axis flip)
    for coord, tile_char in tiles.items():
        x, protocol_y = coord_to_xy(coord)
        alphazero_y = BOARD_SIZE - 1 - protocol_y
        if tile_char == 'b':
            game.tiles[alphazero_y, x] = TILE_BLACK
        elif tile_char == 'g':
            game.tiles[alphazero_y, x] = TILE_GRAY
    
    # Set inventory
    game.tile_counts[0, 0] = stock_black.get('X', 0)
    game.tile_counts[0, 1] = stock_gray.get('X', 0)
    game.tile_counts[1, 0] = stock_black.get('O', 0)
    game.tile_counts[1, 1] = stock_gray.get('O', 0)
    
    # Set current player
    game.current_player = P1 if turn == 'X' else P2
    
    return game


def action_to_protocol(action_hash):
    """Convert action_hash to protocol format
    
    action_hash = move_idx * 51 + tile_idx
    Coordinate conversion: AlphaZero y -> Protocol y
    Format: "origin,target tile_coordtile_color" or "origin,target -"
    """
    move_idx = action_hash // 51
    tile_idx = action_hash % 51
    
    from_idx = move_idx // 25
    to_idx = move_idx % 25
    
    fx, fy = from_idx % 5, from_idx // 5
    tx, ty = to_idx % 5, to_idx // 5
    
    # Convert to protocol coordinates (flip y-axis)
    protocol_fy = BOARD_SIZE - 1 - fy
    protocol_ty = BOARD_SIZE - 1 - ty
    
    origin = xy_to_coord(fx, protocol_fy)
    target = xy_to_coord(tx, protocol_ty)
    
    # Add tile placement
    if tile_idx == 0:
        # No tile placement
        return f"{origin},{target} -1"
    else:
        if tile_idx <= 25:
            tile_pos_idx = tile_idx - 1
            tile_color = 'b'
        else:
            tile_pos_idx = tile_idx - 26
            tile_color = 'g'
        
        tile_x, tile_y = tile_pos_idx % 5, tile_pos_idx // 5
        protocol_tile_y = BOARD_SIZE - 1 - tile_y
        tile_coord = xy_to_coord(tile_x, protocol_tile_y)
        return f"{origin},{target} {tile_coord}{tile_color}"


class AlphaZeroClient:
    def __init__(self, host, port, role, name, model_path, num_simulations, num_games=1):
        self.host = host
        self.port = port
        self.role = role.upper()
        self.name = name
        self.num_simulations = num_simulations
        self.num_games = num_games
        self.games_played = 0
        self.my_role = "?"
        self.awaiting = False
        self.history = deque(maxlen=HISTORY_SIZE)
        self.last_game = None
        self.last_status = "ongoing"
        
        # Load model
        device = torch.device(training_config.DEVICE)
        self.device = device
        self.model = ContrastDualPolicyNet().to(device)
        
        if Path(model_path).exists():
            self.model.load_state_dict(torch.load(model_path, map_location=device, weights_only=True))
            print(f"[AlphaZero] Model loaded from {model_path}")
        else:
            print(f"[AlphaZero] Warning: Model not found at {model_path}")
        
        self.model.eval()
        self.mcts = MCTS(network=self.model, device=device)
    
    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        self.buffer = ""
        print(f"Connected to {self.host}:{self.port}")
    
    def recv_line(self):
        while True:
            pos = self.buffer.find('\n')
            if pos >= 0:
                line = self.buffer[:pos]
                self.buffer = self.buffer[pos+1:]
                return line
            chunk = self.sock.recv(4096)
            if not chunk:
                return None
            self.buffer += chunk.decode('utf-8', errors='replace')
    
    def send(self, msg):
        self.sock.sendall(msg.encode('utf-8'))
    
    def handle_state(self, lines):
        turn, status, pieces, tiles, stock_black, stock_gray = parse_state_block(lines)
        
        print(f"\n[STATE] Turn:{turn} Status:{status}")
        
        # Update last status
        self.last_status = status
        
        if status != 'ongoing' or turn != self.my_role or self.awaiting:
            self.awaiting = False
            return
        
        try:
            game = snapshot_to_game(turn, pieces, tiles, stock_black, stock_gray)
            
            # Update history
            if self.last_game is not None:
                self.history.appendleft((
                    self.last_game.pieces.copy(),
                    self.last_game.tiles.copy(),
                    self.last_game.tile_counts.copy()
                ))
            
            # Fill history
            game.history = deque(self.history, maxlen=HISTORY_SIZE)
            while len(game.history) < HISTORY_SIZE:
                game.history.append((
                    game.pieces.copy(),
                    game.tiles.copy(),
                    game.tile_counts.copy()
                ))
            
            # MCTS search
            policy, _ = self.mcts.search(game, self.num_simulations)
            
            if not policy:
                print("[AlphaZero] No legal moves")
                return
            
            best_action = max(policy.items(), key=lambda x: x[1])[0]
            move_str = action_to_protocol(best_action)
            
            print(f"[AlphaZero] Playing: {move_str}")
            self.send(f"MOVE {move_str}\n")
            self.awaiting = True
            self.last_game = game
            
        except Exception as e:
            print(f"[AlphaZero] Error: {e}")
            import traceback
            traceback.print_exc()
    
    def run(self):
        self.connect()
        self.send(f"ROLE {self.role} {self.name} alphazero\n")
        
        try:
            while True:
                line = self.recv_line()
                if line is None:
                    break
                
                if line == "STATE":
                    block = []
                    while True:
                        next_line = self.recv_line()
                        if next_line is None or next_line == "END":
                            break
                        block.append(next_line)
                    
                    old_status = self.last_status
                    
                    self.handle_state(block)
                    
                    # Check if game just ended
                    if old_status == "ongoing" and self.last_status != "ongoing":
                        self.games_played += 1
                        print(f"[AlphaZero] Game {self.games_played}/{self.num_games} finished: {self.last_status}")
                        
                        # Check if we should continue
                        if self.games_played < self.num_games:
                            print(f"[AlphaZero] Preparing for next game...")
                            self.history.clear()
                            self.last_game = None  # Reset last_game for new game
                            self.send("READY\n")
                        else:
                            print(f"[AlphaZero] All {self.num_games} games completed. Exiting.")
                            break
                
                elif line.startswith("INFO "):
                    msg = line[5:]
                    print(f"[INFO] {msg}")
                    if msg.startswith("You are "):
                        self.my_role = msg[8:9].upper()
                
                elif line.startswith("ERROR "):
                    print(f"[ERROR] {line[6:]}")
                
                else:
                    print(f"[SERVER] {line}")
        
        except KeyboardInterrupt:
            print("\n[AlphaZero] Interrupted")
        finally:
            self.sock.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--role", default="X")
    parser.add_argument("--name", default="AlphaZero")
    parser.add_argument("--model", default=None)
    parser.add_argument("--simulations", type=int, default=100)
    parser.add_argument("--games", type=int, default=1, help="Number of games to play")
    args = parser.parse_args()
    
    if args.model is None:
        model_path = Path(__file__).parent.parent / "ai" / "contrast_alphazero" / "contrast_model_final.pth"
    else:
        model_path = Path(args.model)
    
    client = AlphaZeroClient(
        host=args.host,
        port=args.port,
        role=args.role,
        name=args.name,
        model_path=str(model_path),
        num_simulations=args.simulations,
        num_games=args.games
    )
    client.run()


if __name__ == "__main__":
    main()
