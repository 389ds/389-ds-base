# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Keep an incremental replication AddResponse pending without delaying the add."""

import select
import socket
import threading


def _ber_bounds(data, offset=0):
    """Return the value's start/end offsets, or None for an incomplete header."""
    if len(data) < offset + 2:
        return None
    length = data[offset + 1]
    start = offset + 2
    if length & 0x80:
        octets = length & 0x7f
        if not 1 <= octets <= 4:
            raise ValueError("Expected a definite LDAP BER length")
        if len(data) < start + octets:
            return None
        length = int.from_bytes(data[start:start + octets], 'big')
        start += octets
    return start, start + length


class AsyncResultGate:
    """Relay one plain LDAP connection, holding its first AddResponse.

    Point an otherwise idle SIMPLE/LDAP agreement at ``port`` before resuming
    it. Bind, schema and replication extended-operation replies pass through.
    ``held`` is set when an AddResponse is available but has not been forwarded;
    ``release.set()`` forwards it and all subsequent traffic unchanged. The
    caller must close the gate and inspect ``error`` for worker failures.
    """

    def __init__(self, host, port):
        self.held = threading.Event()
        self.release = threading.Event()
        self._stop = threading.Event()
        self.error = None
        self._target = (host, port)
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.bind(('127.0.0.1', 0))
        self._listener.listen(1)
        self._listener.settimeout(0.1)
        self.port = self._listener.getsockname()[1]
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        try:
            while not self._stop.is_set():
                try:
                    client, _ = self._listener.accept()
                    break
                except socket.timeout:
                    continue
            else:
                return
            with client:
                client.settimeout(1)
                with socket.create_connection(self._target, timeout=1) as server:
                    self._relay(client, server)
        except Exception as error:
            if not self._stop.is_set():
                self.error = error
        finally:
            self._listener.close()

    def _relay(self, client, server):
        pending = bytearray()
        while not self._stop.is_set():
            if self.release.is_set() and pending:
                client.sendall(pending)
                pending.clear()
            # Once held, leave later replies in the socket until released.
            readers = [client]
            if not self.held.is_set() or self.release.is_set():
                readers.append(server)
            ready, _, _ = select.select(readers, [], [], 0.1)
            for source in ready:
                data = source.recv(65536)
                if not data:
                    raise ConnectionError("Replication connection closed during response gating")
                if source is client:
                    server.sendall(data)
                elif self.release.is_set():
                    # Release may have been set while select was waiting;
                    # send any partial earlier frame before these new bytes.
                    pending.extend(data)
                    client.sendall(pending)
                    pending.clear()
                else:
                    pending.extend(data)
                    while pending:
                        bounds = _ber_bounds(pending)
                        if bounds is None:
                            break
                        start, end = bounds
                        if pending[0] != 0x30 or end > 16 * 1024 * 1024:
                            raise ValueError("Expected an LDAPMessage of at most 16 MiB")
                        if len(pending) < end:
                            break
                        # LDAPMessage ::= SEQUENCE { messageID INTEGER,
                        #                            protocolOp CHOICE, ... }
                        message_id = _ber_bounds(pending, start)
                        if pending[start] != 0x02 or message_id is None or message_id[1] >= end:
                            raise ValueError("Missing LDAP message ID or protocol operation")
                        if pending[message_id[1]] == 0x69:  # [APPLICATION 9] AddResponse
                            self.held.set()
                            break
                        client.sendall(pending[:end])
                        del pending[:end]

    def close(self):
        """Stop forwarding and close sockets even if the held reply was not released."""
        self._stop.set()
        self._listener.close()
        self._thread.join(timeout=3)
        if self._thread.is_alive():
            raise RuntimeError("Replication response gate did not stop")
