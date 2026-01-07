# 千日手テスト用クライアント
# XもOも同じ手を繰り返すことで千日手を発生させる
import socket
import time

HOST = '127.0.0.1'
PORT = 18799  # テスト用サーバポート
ROLE = 'X'  # X/OどちらでもOK
NAME = 'repetition_tester'

# 2手を交互に繰り返す（例: a1→a2, a2→a1）
REPEAT_MOVES = [
    'MOVE a1 a2',
    'MOVE a2 a1',
]

def send_and_recv(sock, msg):
    sock.sendall((msg + '\n').encode())
    data = sock.recv(4096)
    return data.decode(errors='replace')

def main():
    with socket.create_connection((HOST, PORT)) as sock:
        print(send_and_recv(sock, f'ROLE {ROLE} {NAME}'))
        print(send_and_recv(sock, 'READY'))
        move_idx = 0
        while True:
            resp = sock.recv(4096).decode(errors='replace')
            print(resp.strip())
            if 'turn' in resp or 'ONGOING' in resp.upper():
                # サーバから状態が来たらMOVE
                move = REPEAT_MOVES[move_idx % len(REPEAT_MOVES)]
                print('send:', move)
                sock.sendall((move + '\n').encode())
                move_idx += 1
            if 'draw' in resp.lower() or 'Winner: Draw' in resp:
                print('== DRAW DETECTED ==')
                break
            time.sleep(0.2)

if __name__ == '__main__':
    main()
