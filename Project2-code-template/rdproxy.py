# rdproxy: User space proxy program to emulate packet loss for Project 2
# Author: Boyan Ding

import asyncio
import random
import sys
from queue import SimpleQueue

DROP_RATE = 0.15
random.seed()

LOCAL_HOST = '127.0.0.1'
SERVER_PORT = 5000
PROXY_PORT = 9999

class RDProxyClientProtocol:
    def __init__(self, server, client_addr):
        self.server = server
        self.client_addr = client_addr
        self.transport = None
        self.q = SimpleQueue()

    def connection_made(self, transport):
        self.transport = transport
        self.dispatch_message()

    def queue_message(self, message):
        self.q.put(message)
        self.dispatch_message()

    def dispatch_message(self):
        if self.transport != None:
            while not self.q.empty():
                item = self.q.get()
                if random.random() >= DROP_RATE:
                    self.transport.sendto(item)

    def datagram_received(self, data, addr):
        if random.random() >= DROP_RATE:
            self.server.transport.sendto(data, self.client_addr)

    def error_received(self, exc):
        print('Error received:', exc)

    def connection_lost(self, exc):
        pass

class RDProxyProtocol:
    def __init__(self, loop):
        self.admap = {}
        self.loop = loop

    def connection_made(self, transport):
        self.transport = transport;

    async def datagram_received_async(self, data, addr):
        h = self.admap.get(addr)
        if h == None:
            _, h = await self.loop.create_datagram_endpoint(
                lambda: RDProxyClientProtocol(self, addr),
                remote_addr = (LOCAL_HOST, SERVER_PORT))
            self.admap[addr] = h

        h.queue_message(data)

    def datagram_received(self, data, addr):
        self.loop.create_task(self.datagram_received_async(data, addr))

async def rdproxy_main():
    loop = asyncio.get_running_loop()

    transport, protocol = await loop.create_datagram_endpoint(
        lambda: RDProxyProtocol(loop),
        local_addr=(LOCAL_HOST, PROXY_PORT))

    try:
        await asyncio.sleep(3600)
    finally:
        transport.close()

def main(argv):
    global SERVER_PORT
    global PROXY_PORT
    global DROP_RATE

    if len(argv) != 3:
        print('Usage: rdproxy.py SERVER_PORT PROXY_PORT DROP_RATE')
        sys.exit(-1)

    SERVER_PORT = int(argv[0])
    PROXY_PORT = int(argv[1])
    DROP_RATE = float(argv[2])

    asyncio.run(rdproxy_main())

if __name__ == '__main__':
   main(sys.argv[1:])
