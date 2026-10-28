"""Menu navigation for scenario tests (verified with screenshots)."""
from driver import P1, P2

# Character Selection, pressing LEFT from "Random" (same prefix with or
# without the lf2.net unlock; the unlocked fighters come after Deep)
LEFT_ORDER = ["Davis", "Woody", "Dennis", "Freeze", "Firen", "Louis", "Rudolf",
              "Henry", "John", "Deep", "Bandit", "Hunter", "Mark", "Jack",
              "Sorcerer", "Monk", "Jan", "Knight", "Justin", "Bat", "LouisEX",
              "Firzen", "Julian"]
LF2NET = [0x4C, 0x46, 0x32, 0xBE, 0x4E, 0x45, 0x54]      # typed "lf2.net"


def to_vs_select(g, unlock=True, mode=0):
    """title -> (type lf2.net) -> Game Start -> mode (0 VS, 1 Stage, ...)
    -> character selection"""
    g.step(69)
    if unlock:
        g.type_text(LF2NET)
    g.click(350, 230)
    g.step(40)
    for _ in range(mode):
        g.tap(P1["down"], 2, 10)
    g.tap(P1["attack"], 2, 40)
    g.post(0x200, 0, (540 << 16) | 790)      # park the mouse off the menus


def pick(g, keys, name, team=0):
    g.tap(keys["attack"], 2, 20)                     # join
    for _ in range(LEFT_ORDER.index(name) + 1):
        g.tap(keys["left"], 2, 6)
    g.tap(keys["attack"], 2, 20)                     # fighter
    for _ in range(team):
        g.tap(keys["right"], 2, 6)
    g.tap(keys["attack"], 2, 20)                     # team


def start_fight(g, bg_steps=1, computers=(), easier=0):
    """after all humans confirmed: countdown, computer count (P1 then picks
    each computer's fighter and team), then the Fight!/Reset All/Reset
    Random/Background (Stage)/Difficulty/Exit menu (cursor on Reset Random).
    bg_steps: Right presses on Background (0 = Random)."""
    g.step(130)
    for _ in range(len(computers)):
        g.tap(P1["right"], 2, 6)
    g.tap(P1["attack"], 2, 40)
    for name in computers:
        if name:
            for _ in range(LEFT_ORDER.index(name) + 1):
                g.tap(P1["left"], 2, 6)
        g.tap(P1["attack"], 2, 20)
        g.tap(P1["attack"], 2, 20)
    if bg_steps:
        g.tap(P1["down"], 2, 8)
        for _ in range(bg_steps):
            g.tap(P1["right"], 2, 8)
        g.tap(P1["up"], 2, 8)
    if easier:           # Difficulty row: Attack cycles Difficult > CRAZY! > Easy > Normal
        g.tap(P1["down"], 2, 8)
        g.tap(P1["down"], 2, 8)
        for _ in range(easier):
            g.tap(P1["attack"], 2, 8)
        g.tap(P1["up"], 2, 8)
        g.tap(P1["up"], 2, 8)
    g.tap(P1["up"], 2, 8)
    g.tap(P1["up"], 2, 8)
    g.tap(P1["attack"], 2, 10)


def wait_fight(g, limit=600):
    """step until both fighters stand on the stage (positions set)"""
    for _ in range(limit):
        obs = [o for o in g.objects() if o.type == 0]
        if obs and all(o.x > 0 for o in obs):
            return obs
        g.step()
    raise RuntimeError("fight did not start")
