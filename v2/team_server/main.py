#!/usr/bin/env python3

"""

PTY WebSocket Relay Server

==========================

Architecture

------------

  [C PTY client]  â†â”€â”€â”€â”€ subprotocol: "terminal-protocol" â”€â”€â”€â”€â†’  [This server]  â†â”€â”€â”€â”€â†’  [xterm.js browser]



  â€¢ The C client sends binary PTY output  â†’ server broadcasts to every browser.

  â€¢ Browsers send binary keystrokes       â†’ server forwards to the C client.

  â€¢ Only ONE C PTY client is allowed at a time; unlimited browser viewers.



Quick start

-----------

  pip install websockets

  python server.py



  For TLS (required if C client uses LCCSCF_USE_SSL):

    Either terminate SSL at nginx/caddy in front of this server,

    or uncomment the ssl_context block below and supply cert/key files.



  Change the C client's address/port to match this server:

    connect_info.address = "your-server-ip";

    connect_info.port    = 8765;           // or 443 with TLS

    // remove LCCSCF_USE_SSL if running plain ws://

"""



import asyncio

import logging

import signal

import ssl

from typing import Optional

import websockets

from websockets.legacy.server import WebSocketServerProtocol



# â”€â”€ Logging â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

logging.basicConfig(

    level=logging.INFO,

    format="%(asctime)s  %(levelname)-7s  %(message)s",

    datefmt="%H:%M:%S",

)

log = logging.getLogger("pty-relay")



# â”€â”€ Config â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

HOST = "0.0.0.0"

PORT = 8765



# Uncomment + fill in paths to enable wss://

# ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)

# ssl_context.load_cert_chain("cert.pem", "key.pem")

ssl_context = None



# â”€â”€ State â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

pty_client: Optional[WebSocketServerProtocol] = None

browser_clients: set[WebSocketServerProtocol] = set()

_lock = asyncio.Lock()          # guards pty_client slot





# â”€â”€ Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

def _to_bytes(msg) -> bytes:

    return msg if isinstance(msg, bytes) else msg.encode()





async def _broadcast_to_browsers(data: bytes) -> None:

    """Fan-out PTY output to every connected browser."""

    if browser_clients:

        websockets.broadcast(browser_clients, data)





# â”€â”€ Connection handlers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

async def handle_pty_client(ws: WebSocketServerProtocol) -> None:

    """

    Handle an incoming C PTY client.

    Receives binary shell output and relays it to all browsers.

    """

    global pty_client



    async with _lock:

        if pty_client is not None:

            log.warning("PTY slot occupied â€” rejecting %s", ws.remote_address)

            await ws.close(1008, "PTY slot already occupied")

            return

        pty_client = ws



    log.info("âœ… PTY client connected  %s", ws.remote_address)



    # Notify browsers that a session is live

    if browser_clients:

        websockets.broadcast(

            browser_clients,

            b"\x1b[2J\x1b[H\r\n\x1b[32m[relay] PTY client connected\x1b[0m\r\n",

        )



    try:

        async for message in ws:

            await _broadcast_to_browsers(_to_bytes(message))

    except websockets.exceptions.ConnectionClosed as exc:

        log.info("PTY client disconnected: %s", exc)

    finally:

        async with _lock:

            pty_client = None

        log.info("PTY slot released")



        if browser_clients:

            websockets.broadcast(

                browser_clients,

                b"\r\n\x1b[31m[relay] PTY client disconnected\x1b[0m\r\n",

            )





async def handle_browser(ws: WebSocketServerProtocol) -> None:

    """

    Handle a browser (xterm.js) connection.

    Receives binary keystrokes and forwards them to the active PTY client.

    """

    browser_clients.add(ws)

    log.info("ðŸŒ Browser connected     %s  (viewers: %d)", ws.remote_address, len(browser_clients))



    # Tell the new browser whether a PTY session is already live

    status_msg = (

        b"\x1b[32m[relay] PTY session is active\x1b[0m\r\n"

        if pty_client

        else b"\x1b[33m[relay] Waiting for PTY client to connect...\x1b[0m\r\n"

    )

    await ws.send(status_msg)



    try:

        async for message in ws:

            if pty_client is not None:

                try:

                    await pty_client.send(_to_bytes(message))

                except websockets.exceptions.ConnectionClosed:

                    pass  # PTY disconnected mid-send; state cleaned up in its own handler

    except websockets.exceptions.ConnectionClosed as exc:

        log.info("Browser %s disconnected: %s", ws.remote_address, exc)

    finally:

        browser_clients.discard(ws)

        log.info("Browser removed (remaining viewers: %d)", len(browser_clients))



# -- Subprotocol selector -----------------------------------------------------

def select_subprotocol(connection, subprotocols: list[str]) -> Optional[str]:

    """Return negotiated subprotocol or None."""

    if "terminal-protocol" in subprotocols:

        return "terminal-protocol"

    return None





# -- Router --------------------------------------------------------------------

async def router(ws: WebSocketServerProtocol) -> None:

    """Route based on the negotiated subprotocol."""

    subproto = getattr(ws, 'subprotocol', None)



    log.debug("Connection | negotiated subprotocol='%s' | remote=%s", 

              subproto, ws.remote_address)



    if subproto == "terminal-protocol":

        await handle_pty_client(ws)

    else:

        await handle_browser(ws)





# -- Entry point ---------------------------------------------------------------

async def main() -> None:

    loop = asyncio.get_running_loop()

    stop = loop.create_future()

    for sig in (signal.SIGINT, signal.SIGTERM):

        loop.add_signal_handler(sig, stop.set_result, None)



    scheme = "wss" if ssl_context else "ws"

    log.info("Starting PTY relay server on %s://%s:%d", scheme, HOST, PORT)



    async with websockets.serve(

        router,

        HOST,

        PORT,

        ssl=ssl_context,

        subprotocols=["terminal-protocol"],

        select_subprotocol=select_subprotocol,   # ? Fixed signature

        ping_interval=20,

        ping_timeout=60,

        max_size=2**20,

        compression="deflate",

    ):

        log.info("-" * 70)

        log.info("? PTY Client   ? %s://%s:%d   (subprotocol: terminal-protocol)", 

                 scheme, HOST, PORT)

        log.info("? Browser      ? %s://%s:%d   (no subprotocol)", 

                 scheme, HOST, PORT)

        log.info("-" * 70)

        await stop



    log.info("Server shut down cleanly.")



if __name__ == "__main__":

    asyncio.run(main())

