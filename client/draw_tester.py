# 10連続千日手テスト用クライアント
import socket
import time

HOST = '127.0.0.1'
PORT = 18800  # テスト用サーバポート
ROLE = 'X'
NAME = 'draw_tester'
REPEAT_MOVES = [
    'MOVE a1,a2 -1',
    'MOVE a2,a1 -1',
]

def send_and_recv(sock, msg):
    sock.sendall((msg + '\n').encode())
    data = sock.recv(4096)
    return data.decode(errors='replace')

def play_one_game(sock):
    print(send_and_recv(sock, f'ROLE {ROLE} {NAME} multi'))
    print(send_and_recv(sock, 'READY'))
    move_idx = 0
    while True:
        resp = sock.recv(4096).decode(errors='replace')
        print(resp.strip())
        # 自分の手番のみMOVE送信
        if f"turn={ROLE}" in resp:
            move = REPEAT_MOVES[move_idx % len(REPEAT_MOVES)]
            print('send:', move)
            sock.sendall((move + '\n').encode())
            move_idx += 1
        if 'draw' in resp.lower() or 'Winner: Draw' in resp:
            print('== DRAW DETECTED ==')
            break
        time.sleep(0.2)

def main():
    with socket.create_connection((HOST, PORT)) as sock:
        for i in range(10):
            print(f'=== GAME {i+1} ===')
            play_one_game(sock)
            time.sleep(0.5)

if __name__ == '__main__':
    main()
