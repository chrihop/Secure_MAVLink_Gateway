#!/bin/env python3

import abc
import argparse
import io
import os
import time
import socket
import pickle
import atexit
import threading
import tqdm
from pprint import pprint
from pymavlink.dialects.v20 import ardupilotmega as mavlink2


class Adapter(metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def send(self, msg):
        pass

    def cleanup(self):
        pass


class StdioAdapter(Adapter):
    def __init__(self):
        self.count = 0

    def send(self, msg):
        mav = mavlink2.MAVLink(io.BytesIO())
        print(f'{self.count}: ' + str(mav.decode(msg)))
        self.count += 1


class PipeAdapter(Adapter):
    def __init__(self, pipe_path):
        self.pipe_path = pipe_path
        self.pipe = None

    def send(self, msg):
        if self.pipe is None:
            os.mkfifo(self.pipe_path)
            self.pipe = open(self.pipe_path, 'wb')
        self.pipe.write(msg)

    def cleanup(self):
        if self.pipe is not None:
            self.pipe.close()
        if os.path.exists(self.pipe_path):
            os.remove(self.pipe_path)


class TcpAdapter(Adapter):
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None

    def send(self, msg):
        if self.sock is None:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            print(f'Connecting to {self.host}:{self.port} ...')
            while True:
                try:
                    self.sock.connect((self.host, self.port))
                    break
                except ConnectionRefusedError:
                    print('.', end='', flush=True)
                    time.sleep(1)
        self.sock.send(msg)

    def cleanup(self):
        if self.sock is not None:
            self.sock.close()


class UdpAdapter(Adapter):
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None

    def send(self, msg):
        if self.sock is None:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.sendto(msg, (self.host, self.port))

    def cleanup(self):
        if self.sock is not None:
            self.sock.close()


class Loader:
    Adapters: dict = {
        'stdio': StdioAdapter,
        'pipe': PipeAdapter,
        'tcp': TcpAdapter,
        'udp': UdpAdapter,
    }

    def __init__(self, path):
        self.path = path
        self.msg_list: list = []

    def load(self):
        with open(self.path, 'rb') as f:
            self.msg_list = pickle.load(f)
        return self.msg_list


class Replayer(threading.Thread):
    progress = None

    def __init__(self, adapter: Adapter, msg_list: list):
        super().__init__()
        if Replayer.progress is None:
            Replayer.progress = tqdm.tqdm(total=len(msg_list))
        self.adapter = adapter
        self.msg_list = msg_list

    def run(self):
        last = 0
        duration = 0
        for ms, msg in self.msg_list:
            time.sleep(max(0, ms - last - duration) / 1000)
            last = ms
            start = time.time()
            self.adapter.send(msg)
            end = time.time()
            duration = int((end - start) * 1000)
            Replayer.progress.update(1)


class Main:
    @staticmethod
    def main():
        argparse.ArgumentParser(
            description='Replay mavlink messages from a file')
        parser = argparse.ArgumentParser()
        parser.add_argument('--load', default='mavmsg_dump.bin',
                            help='Path to the file containing the messages')
        parser.add_argument('--adapter', nargs='+', default=['stdio'],
                            choices=Loader.Adapters.keys(),
                            help='Adapter to use')
        parser.add_argument('--host', default='localhost',
                            help='Host to connect to')
        parser.add_argument('--tcp', default=12001, type=int,
                            help='Port to connect to')
        parser.add_argument('--udp', default=12002, type=int,
                            help='Port to connect to')
        parser.add_argument('--pipe', default=os.path.join(os.getcwd(),
                                                           'mavlink_replay_pipe'),
                            help='Path to the pipe')
        args = parser.parse_args()

        loader = Loader(args.load)
        loader.load()
        adapters = []
        for adapter in args.adapter:
            if adapter == 'stdio':
                adapters.append(StdioAdapter())
            elif adapter == 'pipe':
                adapters.append(PipeAdapter(args.pipe))
            elif adapter == 'tcp':
                adapters.append(TcpAdapter(args.host, args.tcp))
            elif adapter == 'udp':
                adapters.append(UdpAdapter(args.host, args.udp))

        for adapter in adapters:
            atexit.register(adapter.cleanup)

        threads = []
        for adapter in adapters:
            threads.append(Replayer(adapter, loader.msg_list))

        for thread in threads:
            thread.start()


if __name__ == '__main__':
    Main.main()
