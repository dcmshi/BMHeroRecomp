"""Dump the draws in a RenderDoc capture with their post-VS (clip-space) bounds.

Run as:  qrenderdoc --python rd_analyse.py <capture.rdc>

Why this is useful: clip-space `w` is the VIEW-SPACE DEPTH of each vertex, so the
range of w across the level's draws tells you where the geometry actually sits
relative to the camera — in world units, checkable against a model of the camera
you think you set.

That is exactly how the A1.5 camera was confirmed. Our camera at pitch 60 and
ARENA_CAM_DIST 2400 predicts depth = 2486.6 - 0.5*z for a floor point at world z;
for the measured collision floor z in [-950, 950] that is w in [2011.6, 2961.6].
The capture reported w in [2006.6, 2966.6] — a ~5-unit match, proving the drawn
floor and the collision floor are the same square and the camera was where we set
it. Screenshots had been saying otherwise for hours; they were cropped.
"""
import os
import struct
import sys
import traceback

# Paths from the environment (see rd_saveframe.py for why not argv).
CAP = os.environ.get("ARENA_RD_CAP", "")
LOGPATH = os.environ.get("ARENA_RD_LOG") or (
    (os.path.splitext(CAP)[0] + ".analyse.txt") if CAP else "rd_analyse.log")

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

    sdfile = controller.GetStructuredFile()
    acts = list(iter_actions(controller.GetRootActions()))
    draws = [a for a in acts if a.numIndices > 0 and (a.flags & rd.ActionFlags.Drawcall)]
    log("actions:", len(acts), " drawcalls:", len(draws))
    draws.sort(key=lambda d: d.numIndices, reverse=True)

    log("")
    log("=== top draws, with post-VS clip bounds (w = view depth) ===")
    gmin = [1e30] * 4
    gmax = [-1e30] * 4
    for d in draws[:20]:
        controller.SetFrameEvent(d.eventId, True)
        try:
            mesh = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
        except Exception as e:
            log("  eid=%d GetPostVSData failed: %s" % (d.eventId, e))
            continue
        if mesh.vertexResourceId == rd.ResourceId.Null() or mesh.vertexByteStride == 0:
            continue
        n = min(mesh.numIndices, 4000)
        data = controller.GetBufferData(mesh.vertexResourceId,
                                        mesh.vertexByteOffset,
                                        n * mesh.vertexByteStride)
        mins, maxs, cnt = [1e30] * 4, [-1e30] * 4, 0
        for v in range(n):
            off = v * mesh.vertexByteStride
            if off + 16 > len(data):
                break
            vals = struct.unpack_from("<ffff", data, off)
            if vals[3] == 0:
                continue
            for j in range(4):
                mins[j] = min(mins[j], vals[j])
                maxs[j] = max(maxs[j], vals[j])
                gmin[j] = min(gmin[j], vals[j])
                gmax[j] = max(gmax[j], vals[j])
            cnt += 1
        if cnt:
            log("  eid=%-6d n=%-5d x[%8.1f..%8.1f] y[%8.1f..%8.1f] w[%8.1f..%8.1f]"
                % (d.eventId, cnt, mins[0], maxs[0], mins[1], maxs[1], mins[3], maxs[3]))
    log("")
    log("OVERALL  x[%.1f..%.1f] y[%.1f..%.1f] w(view depth)[%.1f..%.1f]"
        % (gmin[0], gmax[0], gmin[1], gmax[1], gmin[3], gmax[3]))

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
