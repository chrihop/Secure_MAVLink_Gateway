#!/bin/env python3

import abc
import argparse
import io
import os
import time
import pickle
import atexit
import logging
from pymavlink import mavutil
from pymavlink.dialects.v20 import ardupilotmega as mavlink2

logging.basicConfig(level=logging.DEBUG,
                    format='[%(asctime)s] %(message)s')
log = logging.getLogger(__name__)


class Header(metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def recv(self) -> bytes:
        pass

    def cleanup(self):
        pass


class PipeHeader(Header):
    def __init__(self, pipe_path):
        self.pipe_path = pipe_path
        self.pipe = None

    def recv(self) -> bytes:
        if self.pipe is None:
            while not os.path.exists(self.pipe_path):
                print('.', end='', flush=True)
                time.sleep(1)
            self.pipe = open(self.pipe_path, 'rb')
        return self.pipe.read()

    def cleanup(self):
        if self.pipe is not None:
            self.pipe.close()


class TcpHeader(Header):
    def __init__(self, host, port):
        self.host: str = host
        self.port: int = port
        self.conn: mavutil.mavtcpin = None
        self.count: int = 0

    def connect(self):
        while True:
            try:
                self.conn = mavutil.mavtcp(f'{self.host}:{self.port}',
                                           autoreconnect=True)
                break
            except ConnectionRefusedError:
                print('.', end='', flush=True)
                time.sleep(1)

    def recv(self) -> bytes:
        if self.conn is None:
            self.connect()
        msg = self.conn.recv_match(blocking=True)
        return msg

    def cleanup(self):
        if self.conn is not None:
            self.conn.close()


class UdpHeader(Header):
    def __init__(self, host, port):
        self.host: str = host
        self.port: int = port
        self.conn: mavutil.mavudp = None
        self.count: int = 0

    def connect(self):
        while True:
            try:
                self.conn = mavutil.mavudp(f'{self.host}:{self.port}', input=False)
                break
            except ConnectionRefusedError:
                print('.', end='', flush=True)
                time.sleep(1)

    def recv(self) -> bytes:
        if self.conn is None:
            self.connect()
        msg = self.conn.recv_match(blocking=True)
        return msg

    def cleanup(self):
        if self.conn is not None:
            self.conn.close()


class SerialHeader(Header):
    def __init__(self, device, baud):
        self.device: str = device
        self.baud: int = baud
        self.conn: mavutil.mavserial = None
        self.count: int = 0

    def recv(self) -> bytes:
        if self.conn is None:
            self.conn = mavutil.mavserial(self.device, baud=self.baud,
                                          autoreconnect=True)
        msg = self.conn.recv_match(blocking=True)
        return msg


class Console:
    Headers = {
        'pipe': PipeHeader,
        'tcp': TcpHeader,
        'udp': UdpHeader,
        'serial': SerialHeader,
    }

    def __init__(self, header: Header, write_to_file: str = None):
        self.header = header
        self.mav = mavlink2.MAVLink(io.BytesIO())
        self.count = 0
        self.write_to_file = write_to_file
        self.binfile: io.FileIO = None
        self.start_tick = time.perf_counter()

    def run(self):
        if self.write_to_file is not None:
            log.addHandler(logging.FileHandler('mavlink.log', mode='w'))
            self.binfile = open(self.write_to_file, 'wb')
        while True:
            msg = self.header.recv()
            # if msg is None or len(msg) == 0:
            #     break
            log.info(f'{self.count}: ' + str(msg))
            if self.binfile is not None:
                pickle.dump((time.perf_counter() - self.start_tick, msg), self.binfile)
            self.count += 1

    def cleanup(self):
        self.header.cleanup()
        if self.binfile is not None:
            self.binfile.close()


class Main:
    @staticmethod
    def main():
        argparse.ArgumentParser(
            description='MAVLink message decoder')
        parser = argparse.ArgumentParser()
        parser.add_argument('--save', type=str, help='Save binary data to file',
                            default=None)
        parser.add_argument('--header', choices=Console.Headers.keys(),
                            default='tcp', help='Header type')
        parser.add_argument('--host', default='localhost', type=str,
                            help='Host to connect to')
        parser.add_argument('--port', default=12011, type=int,
                            help='Port to connect to')
        parser.add_argument('--pipe',
                            default=os.path.join(os.getcwd(),
                                                 'mavlink_replay_pipe'),
                            type=str, help='Pipe to connect to')
        parser.add_argument('--device', default='/dev/ttyUSB0', type=str,
                            help='Serial device to connect to (e.g. /dev/ttyUSB0, /dev/ttyAMA0)')
        parser.add_argument('--baud', default=57600, type=int,
                            help='Serial baud rate')
        args = parser.parse_args()
        if args.header == 'pipe':
            header = Console.Headers[args.header](args.pipe)
        elif args.header in {'tcp', 'udp'}:
            header = Console.Headers[args.header](args.host, args.port)
        else:
            raise ValueError('Unknown header type')
        console = Console(header, write_to_file=args.save)
        atexit.register(console.cleanup)
        console.run()


if __name__ == '__main__':
    Main.main()
