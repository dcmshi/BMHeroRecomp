"""Save a RenderDoc capture's final backbuffer to PNG — the GROUND TRUTH frame.

Run as:  qrenderdoc --python rd_saveframe.py <capture.rdc> [out.png]
(tools/rd-capture.ps1 -Save does this for you.)

This is what settled the A1.5 camera: a PrintWindow screenshot and this image of
the same session disagreed completely, and this one was right. Reach for it
whenever a screenshot-based measurement looks strange.
"""
import os
import sys
import traceback

# Paths come from the environment, not argv: qrenderdoc treats any extra
# argument after `--python script.py` as a capture file to open, so argv never
# reaches the script.
CAP = os.environ.get("ARENA_RD_CAP", "")
OUTPNG = os.environ.get("ARENA_RD_PNG") or (
    os.path.splitext(CAP)[0] + ".png" if CAP else "frame.png")
LOGPATH = os.environ.get("ARENA_RD_LOG") or (os.path.splitext(OUTPNG)[0] + ".savelog.txt")

_log = open(LOGPATH, "w")


def log(*a):
    _log.write(" ".join(str(x) for x in a) + "\n")
    _log.flush()


def iter_actions(actions):
    for a in actions:
        yield a
        for c in iter_actions(a.children):
            yield c


def main():
    import renderdoc as rd
    if not CAP or not os.path.exists(CAP):
        log("FATAL: capture not found:", CAP)
        return 2

    cap = rd.OpenCaptureFile()
    r = cap.OpenFile(CAP, "", None)
    if hasattr(r, "OK") and not r.OK():
        log("FATAL: OpenFile", r)
        return 3
    out = cap.OpenCapture(rd.ReplayOptions(), None)
    r, controller = out if isinstance(out, tuple) else (None, out)
    if controller is None:
        log("FATAL: OpenCapture", r)
        return 4

    acts = list(iter_actions(controller.GetRootActions()))
    controller.SetFrameEvent(acts[-1].eventId, True)   # end of frame

    tex = None
    for t in controller.GetTextures():
        if t.creationFlags & rd.TextureCategory.SwapBuffer:
            tex = t
            break
    if tex is None:
        log("FATAL: no swapchain texture")
        return 5
    log("backbuffer:", tex.width, "x", tex.height, tex.format.Name())

    sav = rd.TextureSave()
    sav.resourceId = tex.resourceId
    sav.mip = 0
    sav.slice.sliceIndex = 0
    sav.destType = rd.FileType.PNG
    log("SaveTexture ->", controller.SaveTexture(sav, OUTPNG), "->", OUTPNG)

    controller.Shutdown()
    cap.Shutdown()
    return 0


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
