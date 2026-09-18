"""Test real Android pose socket with concurrent readers; stop the host first.

Build tests/android/pose_client_concurrent_android.cpp with pose_client.cpp using the
NDK, -std=c++20 -static-libstdc++ -llog and runtime/protocol include paths.
Pass the resulting ARM64 executable as the sole argument.
"""
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time

adb = str(Path(os.environ['LOCALAPPDATA'])/'Android/Sdk/platform-tools/adb.exe')
def run(*args):
    return subprocess.run([adb,'-s','emulator-5580',*args],check=True,timeout=30)

with socket.socket() as server:
    server.bind(('0.0.0.0',38490))
    server.listen(1)
    server.settimeout(20)
    run('push',sys.argv[1],'/data/local/tmp/pose-concurrency')
    run('shell','chmod 755 /data/local/tmp/pose-concurrency')
    def produce():
        connection,_=server.accept()
        with connection:
            connection.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
            try:
                for sequence in range(1,20000):
                    frame=bytearray(2408)
                    struct.pack_into('<IHHQQ',frame,0,0x42525841,5,1,sequence,sequence*1000000)
                    struct.pack_into('<7f',frame,24,float(sequence%1024),-float(sequence%1024),0,0,0,0,1)
                    # Exercise header splits and partial records, not just
                    # conveniently aligned TCP writes.
                    split=(sequence*137)%2407+1
                    connection.sendall(frame[:split])
                    time.sleep(.0001)
                    connection.sendall(frame[split:])
                    time.sleep(.001)
            except (ConnectionError,OSError):
                pass
    thread=threading.Thread(target=produce,daemon=True)
    thread.start()
    run('shell','/data/local/tmp/pose-concurrency')
    thread.join(timeout=2)
