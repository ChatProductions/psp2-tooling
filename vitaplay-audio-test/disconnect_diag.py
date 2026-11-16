#!/usr/bin/env python3
"""v6 logging-only instrumentation for VitaPlay's TCP disconnect cause.
No binary patching, MFi credential changes, or protocol behavior changes."""
from pathlib import Path
source = Path("upstream/vitaplay/src/session.c")
txt = source.read_text()
def one(old, new):
    global txt
    count = txt.count(old)
    if count != 1:
        raise SystemExit(f"expected 1 hit, got {count}: {old[:110]!r}")
    txt = txt.replace(old, new)

one(
    "        int n = recv(fd, chunk, sizeof(chunk), 0);\n        if (n <= 0) break;",
    "        int n = recv(fd, chunk, sizeof(chunk), 0);\n"
    "        if (n <= 0) {\n"
    "            if (n == 0) vp_log(\"v6 event TCP peer EOF (iPhone closed event connection)\");\n"
    "            else vp_log(\"v6 event TCP recv failed errno=%d (closed=%d)\", errno, (int)s->closed);\n"
    "            break;\n"
    "        }")
one(
    "        int n = recv(s->fd, chunk, 16 * 1024, 0);\n        if (n <= 0) break;",
    "        int n = recv(s->fd, chunk, 16 * 1024, 0);\n"
    "        if (n <= 0) {\n"
    "            if (n == 0) vp_log(\"v6 control TCP peer EOF (iPhone closed control connection)\");\n"
    "            else vp_log(\"v6 control TCP recv failed errno=%d (closed=%d)\", errno, (int)s->closed);\n"
    "            break;\n"
    "        }")
one(
    "            if (send_all(s->fd, send_buf->data, send_buf->len) != 0) {\n                used = -1;\n                break;\n            }",
    "            if (send_all(s->fd, send_buf->data, send_buf->len) != 0) {\n"
    "                vp_log(\"v6 control TCP send failed errno=%d (closed=%d)\", errno, (int)s->closed);\n"
    "                used = -1;\n"
    "                break;\n"
    "            }")
source.write_text(txt)
print("v6 diagnostics installed: control/event socket EOF versus errno, send error")
