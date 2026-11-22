#!/usr/bin/env python3
"""Check a bfvplus recording's own-view (cam, camv) and input (in) rows against the rest of the file.

Usage: check_view.py <replay.ndjson>

The view and input reads (replay-recorder-2, items 4-5) were found by static analysis only. A recording
made with them is checked here against what earlier versions already record and the lab trusts: the
recording player's own soldier (`s` rows of his root from his `p` rows), his body's aim and zoom bit
(`st`), and his own rounds (`f` with local). Prints medians; nothing is asserted, the numbers are the
evidence:
  - on foot (his root is a soldier): the camera's height over his soldier's origin (an eye height,
    1.4-1.8 m standing in first person) and its horizontal distance from it (under ~0.5 m in first
    person, metres in third person), per camv view mode;
  - the camera's pitch against 2.5 x his recorded aim pitch (replay-recording.js AIM_PITCH_SCALE);
  - the range of the field of view seen (it narrows while he zooms);
  - his own rounds' times against the fire bit (bit 0) of his `in` words.
"""
import bisect, json, math, statistics, sys
from collections import defaultdict

def rot(q, v):
    # v' = v + w t + q.xyz x t, with t = 2 (q.xyz x v)
    x, y, z, w = q
    t = (2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0]))
    return (v[0] + w * t[0] + y * t[2] - z * t[1], v[1] + w * t[1] + z * t[0] - x * t[2], v[2] + w * t[2] + x * t[1] - y * t[0])

def main(path):
    me = None; roots = []; poses = defaultdict(list); st = []; cams = []; camv = []; ins = []; fires = []
    for line in open(path, encoding='utf-8', errors='replace'):
        try: r = json.loads(line)
        except ValueError: continue
        k, t = r.get('k'), r.get('t', 0)
        if k == 'roster':
            for p in r['p']:
                if p[4]: me = p[0]
        elif k == 'p' and me is not None:
            for p in r['p']:
                if p[0] == me: roots.append((t, p[3]))
        elif k == 's':
            for o in r['o']: poses[o[0]].append((t, o[1:4], o[4:8]))
        elif k == 'st': st.append((t, r['o']))
        elif k == 'cam': cams.append((t, r['o']))
        elif k == 'camv': camv.append((t, r.get('mode'), r.get('cam'), r.get('root')))
        elif k == 'in': ins.append((t, r['v']))
        elif k == 'f' and r.get('local'): fires.append(t)
    print(f'{len(cams)} cam, {len(camv)} camv, {len(ins)} in rows; recording player pid {me}')
    def at(lst, t):
        i = bisect.bisect_right([x[0] for x in lst], t) - 1
        return lst[i] if i >= 0 else None
    rootT = [x[0] for x in roots]
    by_mode = defaultdict(lambda: ([], []))
    pitch_err = []
    for t, o in cams:
        i = bisect.bisect_right(rootT, t) - 1
        if i < 0: continue
        root = roots[i][1]
        pl = poses.get(root)
        if not pl: continue
        j = bisect.bisect_right([x[0] for x in pl], t) - 1
        if j < 0 or t - pl[j][0] > 1.0: continue
        p = pl[j][1]
        v = at(camv, t)
        mode = v[1] if v else None
        dy = o[1] - p[1]; dh = math.hypot(o[0] - p[0], o[2] - p[2])
        by_mode[mode][0].append(dy); by_mode[mode][1].append(dh)
        f = rot(o[3:7], (0, 0, 1))
        cam_pitch = math.degrees(math.asin(max(-1, min(1, f[1]))))
        for ts, rows in reversed(st):
            if ts > t: continue
            row = next((x for x in rows if x[0] == root), None)
            if row is not None:
                if t - ts < 1.0: pitch_err.append(cam_pitch - 2.5 * row[3])
                break
    for mode, (dys, dhs) in sorted(by_mode.items(), key=lambda x: str(x[0])):
        print(f'view mode {mode}: {len(dys)} rows over his root: height median {statistics.median(dys):.2f} m, '
              f'horizontal median {statistics.median(dhs):.2f} m')
    if pitch_err:
        print(f'camera pitch - 2.5 x aim pitch: median {statistics.median(pitch_err):.2f} deg over {len(pitch_err)} rows '
              f'(sign: the viewer may count pitch the other way; a constant near 0 or a clean sign flip both mean it tracks)')
    if ins and fires:
        fire_on = [t for t, v in ins if v[6] & 1]
        near = sum(1 for tf in fires if any(abs(tf - ti) < 0.2 for ti in fire_on))
        print(f'his rounds with a fire bit in an in row within 0.2 s: {near} of {len(fires)}')
    if cams:
        fovs = [o[7] for _, o in cams if o[7] is not None]
        if fovs: print(f'fov seen: min {min(fovs):.3f} max {max(fovs):.3f} rad')

if __name__ == '__main__':
    main(sys.argv[1])
