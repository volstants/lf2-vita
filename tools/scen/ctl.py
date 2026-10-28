"""Fighter control on top of driver.Game: walk to a target, face it, and
enter command inputs (D>A, D^J, ...). Direction '>' is toward the facing."""
from driver import P1, P2


def obj(g, idx):
    for o in g.objects():
        if o.idx == idx:
            return o
    return None


class Pad:
    def __init__(self, g, keys, idx):
        self.g, self.k, self.idx = g, keys, idx
        self.dirs = set()

    def me(self):
        return obj(self.g, self.idx)

    def _hold(self, names):
        for n in list(self.dirs - names):
            self.g.up(self.k[n]); self.dirs.discard(n)
        for n in names - self.dirs:
            self.g.down(self.k[n]); self.dirs.add(n)

    def stop(self):
        self._hold(set())

    def approach(self, tidx, dx=60, tol=6, ztol=2, limit=400, side=None):
        """walk until |x - (target.x -/+ dx)| <= tol and |dz| <= ztol, then
        face the target. side: +1 stand right of target, -1 left, None keep."""
        g = self.g
        for _ in range(limit):
            m, t = self.me(), obj(g, tidx)
            if m is None or t is None:
                break
            s = side if side is not None else (1 if m.x >= t.x else -1)
            ex = (t.x + s * dx) - m.x
            ez = t.z - m.z
            want = set()
            if abs(ex) > tol:
                want.add("right" if ex > 0 else "left")
            if abs(ez) > ztol:
                want.add("down" if ez > 0 else "up")
            if not want:
                break
            self._hold(want)
            g.step()
        self.stop()
        g.step(3)
        self.face(tidx)

    def face(self, tidx):
        m, t = self.me(), obj(self.g, tidx)
        if m is None or t is None:
            return
        want_left = t.x < m.x
        if bool(m.facing) != want_left:
            self.g.tap(self.k["left" if want_left else "right"], 2, 3)

    def fwd(self):
        return "left" if self.me().facing else "right"

    def back(self):
        return "right" if self.me().facing else "left"

    def cmd(self, notation, hold=2, gap=1):
        """D>A, D^J, DvA, DJA, D<J ... entered as separate taps"""
        names = {"D": "defend", "A": "attack", "J": "jump", "^": "up", "v": "down"}
        seq = []
        for c in notation:
            if c == ">":
                seq.append(self.fwd())
            elif c == "<":
                seq.append(self.back())
            else:
                seq.append(names[c])
        self.g.seq([self.k[n] for n in seq], hold, gap)

    def tap(self, name, hold=2, after=4):
        self.g.tap(self.k[name], hold, after)

    def run_toward(self, tidx, frames=20):
        """double tap forward and keep holding (run)"""
        k = self.k[self.fwd()]
        self.g.tap(k, 1, 1)
        self.g.down(k)
        self.g.step(frames)
        self.g.up(k)


def settle(g, frames=60):
    g.step(frames)


STANDING = set(range(0, 5))


def wait_idle(g, idxs, limit=300):
    """until every fighter in idxs is standing (frames 0-4) on the ground"""
    for _ in range(limit):
        obs = {o.idx: o for o in g.objects()}
        if all(i in obs and obs[i].frame in STANDING and obs[i].y == 0 for i in idxs):
            return True
        g.step()
    return False


def reset(g, idxs=(0, 1), frames=20):
    """F7 (HP/MP full), then wait until everyone stands"""
    wait_idle(g, idxs)
    g.tap(0x76, 2, frames)
    wait_idle(g, idxs)
