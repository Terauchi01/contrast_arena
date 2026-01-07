#!/usr/bin/env python3
import socket

HOST='127.0.0.1'
PORT=18888
ROLE='O'
NAME='illegal_o'

sock=socket.create_connection((HOST,PORT))
buf=''

def recv_line():
    global buf
    while True:
        pos=buf.find('\n')
        if pos>=0:
            line=buf[:pos]
            buf=buf[pos+1:]
            return line
        data=sock.recv(4096)
        if not data:
            return None
        buf+=data.decode('utf-8',errors='replace')

# send ROLE and READY
sock.sendall(f'ROLE {ROLE} {NAME} test multi\n'.encode())
# read initial responses
while True:
    line=recv_line()
    if line is None:
        break
    # wait for STATE block
    if line=='STATE':
        # consume block
        state_block=[]
        while True:
            nl=recv_line()
            if nl is None or nl=='END':
                break
            state_block.append(nl)
        # check turn
        for l in state_block:
            if l.startswith('turn=') and 'turn=O' in l:
                # It's our turn immediately; send illegal move
                sock.sendall(b'MOVE e1,e2 e1b\n')
                print('sent illegal move')
                sock.close()
                raise SystemExit
    # also look for a direct turn tag in single-line messages
    if 'turn=O' in line:
        sock.sendall(b'MOVE e1,e2 e1b\n')
        print('sent illegal move')
        sock.close()
        raise SystemExit

sock.close()
