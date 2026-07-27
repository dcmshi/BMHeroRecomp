"""Trigger a RenderDoc capture on the already-hooked game, without window focus.

Driven by tools/rd-capture.ps1; run as:  qrenderdoc --python rd_trigger.py

renderdoccmd has no "trigger capture" verb, and F12 needs foreground focus which
a background process cannot reliably take (SetForegroundWindow is restricted from
a non-foreground process). But the hooked process registers a target-control
channel, and qrenderdoc's EMBEDDED Python can connect to it and trigger a capture
directly. No system Python or packages are needed.

Everything is wrapped in try/except writing to a log FILE: qrenderdoc is a GUI
app so stdout is not visible, and __file__ is not guaranteed to be defined in
this embedded interpreter (an earlier version died on exactly that and produced
no output at all, which is a miserable thing to debug).
"""
import os
import sys
import time
import traceback


def _logpath():
    """Where to write the log — from the ARENA_RD_LOG env var.

    Env, not argv: qrenderdoc's usage is `qrenderdoc [options] filename`, so an
    extra argument after `--python script.py` is taken as a CAPTURE FILE to open
    and never reaches the script. And do not fall back to argv[0]'s directory:
    that is qrenderdoc.exe under Program Files, which is not writable, so open()
    raises at import time and the script dies leaving NO log at all — which looks
    exactly like "the script never ran".
    """
    v = os.environ.get("ARENA_RD_LOG")
    if v:
        return v
    try:
        return os.path.join(os.path.dirname(os.path.abspath(__file__)), "rd_trigger.log")
    except NameError:
        return os.path.join(os.getcwd(), "rd_trigger.log")


_log = open(_logpath(), "w")


def log(*a):
    _log.write(" ".join(str(x) for x in a) + "\n")
    _log.flush()


def main():
    import renderdoc as rd
    log("python:", sys.version)

    ident, targets = 0, []
    while True:
        ident = rd.EnumerateRemoteTargets("localhost", ident)
        if ident == 0:
            break
        targets.append(ident)
        if len(targets) > 8:
            break
    log("targets:", targets)
    if not targets:
        log("FATAL: no hooked target (is the game running under renderdoccmd capture?)")
        return 3

    conn = rd.CreateTargetControl("localhost", targets[0], "claude", True)
    if conn is None:
        log("FATAL: CreateTargetControl returned None")
        return 4
    log("connected:", conn.GetTarget(), "pid", conn.GetPID())

    conn.TriggerCapture(1)
    log("TriggerCapture sent")

    deadline = time.time() + 60
    while time.time() < deadline:
        msg = conn.ReceiveMessage(None)
        if msg is None:
            time.sleep(0.05)
            continue
        if msg.type == rd.TargetControlMessageType.NewCapture:
            log("CAPTURED path:", msg.newCapture.path,
                "frame:", msg.newCapture.frameNumber)
            conn.Shutdown()
            return 0
        if msg.type == rd.TargetControlMessageType.Disconnected:
            log("FATAL: target disconnected before the capture arrived")
            conn.Shutdown()
            return 6
    log("FATAL: timed out waiting for the capture")
    conn.Shutdown()
    return 5


rc = 1
try:
    rc = main()
except Exception:
    log("EXCEPTION:\n" + traceback.format_exc())
    rc = 9
finally:
    log("exit", rc)
    _log.close()
    os._exit(rc)
