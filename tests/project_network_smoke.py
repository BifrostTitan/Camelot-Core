"""Exercise the installed project's real BeginPlay connection using launcher overrides and a modern fragmented handshake."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from unreal_engine import resolve_editor_executable
engine_editor = str(resolve_editor_executable())

def exact(connection, count):
    result = b""
    while len(result) < count:
        part = connection.recv(count - len(result))
        if not part:
            raise AssertionError("Client closed early")
        result += part
    return result


def request(connection, opcode):
    header = exact(connection, 10)
    size, sequence, session, parameter, code = struct.unpack(">HHHHH", header)
    body = exact(connection, size + 2)
    assert code == opcode, (code, opcode)
    a = c = 0x7E
    for byte in header + body[:-2]:
        a = (a + byte) & 255
        c = (c + a) & 255
    assert struct.unpack(">H", body[-2:])[0] == (c - ((a + c) << 8)) & 65535
    return body[:-2]


def reply(connection, opcode, payload):
    frame = struct.pack(">HB", len(payload), opcode) + payload
    for start in range(0, len(frame), 7):
        connection.sendall(frame[start:start + 7])



project = Path(sys.argv[1])
log = project.parent / 'Saved/Logs/CamelotNetworkRuntimeSmoke.log'
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(90)
    process = subprocess.Popen([
        engine_editor, str(project),
        '/Game/DAoC/Maps/DAoCWorld', '-game', '-NullRHI', '-unattended', '-nosound', '-nosplash',
        '-seconds=15', '-CamelotAutoConnect', '-CamelotServer=127.0.0.1',
        '-CamelotPort='+str(listener.getsockname()[1]), '-CamelotUsername=account', '-CamelotCharacter=Tester', '-NoShaderCompile', '-abslog=' + str(log),
    ], env={**os.environ, 'CAMELOT_PASSWORD':'secret'}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        with listener.accept()[0] as connection:
            connection.settimeout(20)
            assert request(connection, 0xF4) == bytes([0, 7, 1, 7, 4, ord('d')])
            reply(connection, 0x22, bytes([1, 0x32, 1, 74, 0]))
            login = request(connection, 0xA7)
            assert login[5:25].rstrip(b'\0').decode() == 'secret', 'Runtime password config mismatch'
            assert login[75:95].rstrip(b'\0').decode() == 'account', 'Runtime username mismatch'
            reply(connection, 0x2A, bytes([1, 1, 74, 0]))
            assert request(connection, 0xFC)[3:].rstrip(b'\0').decode() == 'account' + '-S'
            characters = bytearray(24 + 10 * 184 + 104)
            name = b'Tester'
            characters[24:24+len(name)] = name
            reply(connection, 0xFD, characters)
            assert request(connection, 0x10)[4:].rstrip(b'\0') == name
            reply(connection, 0x28, struct.pack('<H', 42))
            assert request(connection, 0xBF) == bytes([1, 74, ord('d'), 0])
            reply(connection, 0x2D, b'\0')
            assert request(connection, 0xD4) == b''
            reply(connection, 0x20, struct.pack('>HHIIHBB', 7, 500, 550000, 490000, 1024, 0, 0))
            assert request(connection, 0xE8) == b''
            reply(connection, 0x2B, b'\0')
            # Keep the mock connection alive until the timed runtime exits.
            while process.poll() is None:
                try:
                    connection.settimeout(0.5)
                    data = connection.recv(8192)
                    if not data:
                        break
                except socket.timeout:
                    continue
        assert process.wait(timeout=60) == 0, 'Unreal runtime exited with an error'
        text = log.read_text(errors='replace')
        assert 'Camelot:' not in '\n'.join(line for line in text.splitlines() if 'Error:' in line), 'Runtime network error; inspect log'
        assert 'Camelot session: Playing' in text
        print('Game-instance runtime passed: auto-connect, config password, login, character selection, world/player init')
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
