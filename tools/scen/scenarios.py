"""Scenario tests: special moves, character/object interactions, status
effects, strong knockback and transformations.

Each scenario is a VS match between two human players (P1 = object 0,
P2 = object 1) driven closed-loop by reading the game state. `run.py` records
the input as an events file and the per-frame object state; the same events
are replayed on the original lf2.exe (Unicorn) for comparison.
"""
from ctl import STANDING, Pad, obj, reset, wait_idle
from driver import P1, P2, VK_F
from menus import pick, start_fight, to_vs_select, wait_fight


def match(g, p1, p2, bg=1):
    to_vs_select(g)
    pick(g, P1, p1)
    pick(g, P2, p2)
    start_fight(g, bg_steps=bg)
    wait_fight(g)
    return Pad(g, P1, 0), Pad(g, P2, 1)


def hit(g, pad, target, notation, label, dx, after=70, side=None, shots=(8, 20)):
    """one test: reset, place `pad` dx away from target (facing it), input"""
    reset(g)
    pad.approach(target, dx=dx, side=side)
    g.mark(label)
    pad.cmd(notation)
    for k in range(after):
        if k in shots:
            g.shot("%05d_%s.ppm" % (g.frame, label.split()[0]))
        g.step()


def basic(g, pad, target, keys, label, dx, after=60, side=None, hold=2, gap=4):
    reset(g)
    pad.approach(target, dx=dx, side=side)
    g.mark(label)
    for k in keys:
        if k == "run":
            pad.run_toward(target, 12)
        else:
            pad.tap(k, hold, gap)
    for k in range(after):
        if k in (6, 18):
            g.shot("%05d_%s.ppm" % (g.frame, label.split()[0]))
        g.step()


# ── S1: Davis x Firen — projectiles, combos, launch, fire status ───────────
def s1_davis_firen(g):
    a, b = match(g, "Davis", "Firen")
    hit(g, a, 1, "D>A", "davis_D>A_energy_blast", 160)
    hit(g, a, 1, "DvA", "davis_DvA_many_punch", 45, after=90)
    hit(g, a, 1, "D^A", "davis_D^A_dragon_punch_launch", 35, after=100)
    basic(g, a, 1, ["attack"] * 4, "davis_jabs_dizzy_super_punch", 30, gap=9, after=90)
    jump_attack(g, a, 1, "davis_jump_attack")
    basic(g, a, 1, ["run", "attack"], "davis_run_attack", 200, gap=2)
    dash_attack(g, a, 1, "davis_dash_attack")
    hit(g, b, 0, "D>A", "firen_D>A_fireball_burn", 170, after=90)
    hit(g, b, 0, "D>J", "firen_D>J_burn_run", 200, after=110)
    hit(g, b, 0, "DvJ", "firen_DvJ_flame", 90, after=110)
    hit(g, b, 0, "D^J", "firen_D^J_explosion", 40, after=110)
    reset(g)


# ── S2: Freeze x Woody — ice status, shatter, whirlwind, teleport ──────────
def s2_freeze_woody(g):
    a, b = match(g, "Freeze", "Woody")
    hit(g, a, 1, "D>A", "freeze_D>A_iceball_frozen", 160, after=40)
    g.mark("woody_frozen_then_hit_hard")
    a.approach(1, dx=40)
    a.tap("attack", 2, 4); a.tap("attack", 2, 4); a.tap("attack", 2, 4)
    g.step(90)
    hit(g, a, 1, "D^J", "freeze_D^J_whirlwind_lift", 60, after=140)
    hit(g, a, 1, "DvJ", "freeze_DvJ_ice_sword", 80, after=60)
    basic(g, a, 1, ["attack", "attack"], "freeze_sword_attacks", 45, gap=6)
    hit(g, a, 1, "D>J", "freeze_D>J_down_punch_ice_column", 90, after=110)
    hit(g, b, 0, "D>A", "woody_D>A_ball", 160)
    hit(g, b, 0, "DvA", "woody_DvA_cleg", 60)
    hit(g, b, 0, "D>J", "woody_D>J_fly_crash", 120, after=90)
    hit(g, b, 0, "D^J", "woody_D^J_teleport_behind", 250, after=60)
    hit(g, b, 0, "DvJ", "woody_DvJ_teleport_ally", 250, after=60)
    reset(g)


# ── S3: Rudolf x John — catch, transformation, clones, invisibility, heal ─
def make_dizzy_and_catch(g, pad, target):
    """three jabs 9 frames apart leave the victim dizzy (frames 226-229:
    `fall` saturated at 60); walking into him catches (120-123 / 130-144)"""
    pad.approach(target, dx=30, ztol=1)
    for _ in range(3):
        pad.tap("attack", 2, 9)
    k = pad.k[pad.fwd()]
    g.down(k)
    for _ in range(20):
        g.step()
        if pad.me().frame in (120, 121, 122, 123):
            break
    g.up(k)
    g.step(2)


def jump_attack(g, pad, target, label, dx=175, after=80):
    """forward jump; kick on the way down, next to the target"""
    reset(g)
    pad.approach(target, dx=dx, ztol=1)
    g.mark(label)
    k = pad.k[pad.fwd()]
    g.down(k)
    g.step()
    pad.tap("jump", 2, 1)
    for _ in range(40):
        m, t = pad.me(), obj(g, target)
        if m.y < 0 and m.vy > 0 and abs(m.x - t.x) < 75:
            pad.tap("attack", 2, 1)
            break
        g.step()
    g.up(k)
    for i in range(after):
        if i in (4, 14):
            g.shot("%05d_%s.ppm" % (g.frame, label))
        g.step()


def dash_attack(g, pad, target, label, dx=380, after=90):
    """run, jump while running (dash), attack on the way down near the target"""
    reset(g)
    pad.approach(target, dx=dx, ztol=1)
    g.mark(label)
    k = pad.k[pad.fwd()]
    g.tap(k, 1, 1)
    g.down(k)
    for _ in range(80):
        m, t = pad.me(), obj(g, target)
        if abs(m.x - t.x) < 180:
            break
        g.step()
    pad.tap("jump", 2, 1)
    for _ in range(40):
        m, t = pad.me(), obj(g, target)
        if m.y < 0 and abs(m.x - t.x) < 140:
            pad.tap("attack", 2, 1)
            break
        g.step()
    g.up(k)
    for i in range(after):
        if i in (4, 14):
            g.shot("%05d_%s.ppm" % (g.frame, label))
        g.step()


def s3_rudolf_john(g):
    a, b = match(g, "Rudolf", "John")
    reset(g)
    g.mark("rudolf_dizzy_catch_john")
    make_dizzy_and_catch(g, a, 1)
    g.shot("%05d_rudolf_catching.ppm" % g.frame)
    g.mark("rudolf_DJA_transform_into_john")
    a.cmd("DJA")
    g.step(60)
    g.shot("%05d_rudolf_as_john.ppm" % g.frame)
    hit(g, a, 1, "D>A", "rudolf_as_john_D>A", 150, after=80)
    hit(g, a, 1, "D>J", "rudolf_as_john_force_field", 150, after=80)
    reset(g)
    g.mark("rudolf_DJA_transform_back")
    a.cmd("DJA")
    g.step(80)
    hit(g, a, 1, "DvJ", "rudolf_DvJ_clones", 150, after=120)
    hit(g, a, 1, "D^J", "rudolf_D^J_disappear", 150, after=120)
    hit(g, a, 1, "D>A", "rudolf_D>A_punch", 40, after=70)
    hit(g, b, 0, "D>A", "john_D>A_energy_disk", 160, after=80)
    hit(g, b, 0, "D^A", "john_D^A_biscuit", 160, after=90)
    hit(g, b, 0, "D>J", "john_D>J_force_field", 160, after=110)
    basic(g, a, 1, ["attack", "attack", "attack"], "damage_john_before_heal", 45, gap=5)
    wait_idle(g, (0, 1))
    g.mark("john_DvJ_heal_self")
    b.cmd("DvJ")
    g.step(120)
    reset(g)


# ── S4: Louis x Deep — LouisEX transformation, grabs, sword dash ───────────
def s4_louis_deep(g):
    a, b = match(g, "Louis", "Deep")
    hit(g, a, 1, "D>A", "louis_D>A_blastpush", 50)
    hit(g, a, 1, "D>J", "louis_D>J_1000foot", 60, after=90)
    hit(g, a, 1, "D^J", "louis_D^J_r_catch", 40, after=110)
    reset(g)
    g.mark("louis_DJA_transform_to_louisEX")
    a.cmd("DJA")
    g.step(100)
    g.shot("%05d_louisEX.ppm" % g.frame)
    hit(g, a, 1, "D>A", "louisEX_D>A_air_push", 60, after=90)
    hit(g, a, 1, "DvA", "louisEX_DvA_up_spear", 45, after=100)
    basic(g, a, 1, ["attack", "attack", "attack"], "louisEX_combo", 45, gap=5)
    hit(g, b, 0, "D>A", "deep_D>A_blast", 160)
    hit(g, b, 0, "DvA", "deep_DvA_jump_sword", 60)
    hit(g, b, 0, "D>J", "deep_D>J_dash_sword", 150, after=90)
    reset(g)


# ── S5: Henry x Dennis — weapons (F8), throws, arrows, flute, chase ball ───
def weapons(g, types):
    return [o for o in g.objects() if o.type in types and o.y == 0]


def pick_up(g, pad, w, label):
    pad.approach(w.idx, dx=0, tol=4, ztol=2)
    g.mark(label)
    pad.tap("attack", 2, 20)


def s5_henry_dennis(g):
    a, b = match(g, "Henry", "Dennis")
    reset(g)
    g.tap(VK_F[8], 2, 90)                       # drop the weapons
    g.mark("F8_weapons_dropped")
    light = weapons(g, (1,))
    if light:
        w = min(light, key=lambda o: abs(o.x - a.me().x) + abs(o.z - a.me().z))
        pick_up(g, a, w, "henry_picks_light_weapon_id%d" % w.id)
        basic(g, a, 1, ["attack", "attack"], "henry_hits_with_weapon", 55, gap=8)
        basic(g, a, 1, ["run", "attack"], "henry_run_throw_weapon", 220, gap=2, after=90)
    heavy = weapons(g, (2,))
    if heavy:
        w = min(heavy, key=lambda o: abs(o.x - b.me().x) + abs(o.z - b.me().z))
        pick_up(g, b, w, "dennis_lifts_heavy_id%d" % w.id)
        b.approach(0, dx=120)
        g.mark("dennis_throws_heavy")
        b.tap("attack", 2, 90)
    drink = weapons(g, (6,))
    if drink:
        reset(g)
        basic(g, b, 0, ["attack", "attack", "attack"], "damage_henry_before_drink", 45, gap=5)
        wait_idle(g, (0, 1))
        w = min(drink, key=lambda o: abs(o.x - a.me().x))
        pick_up(g, a, w, "henry_picks_drink_id%d" % w.id)
        g.mark("henry_drinks")
        for i in range(4):
            a.tap("attack", 2, 25)
    g.tap(VK_F[9], 2, 30)                       # remove all weapons
    hit(g, a, 1, "D>A", "henry_D>A_blastpush", 50)
    hit(g, a, 1, "D>J", "henry_D>J_super_arrow", 250, after=80)
    hit(g, a, 1, "DJA", "henry_DJA_5_arrows", 250, after=80)
    hit(g, a, 1, "D^J", "henry_D^J_flute_lift", 150, after=160)
    hit(g, b, 0, "D>A", "dennis_D>A_ball", 160)
    hit(g, b, 0, "D^A", "dennis_D^A_chase_ball", 220, after=110)
    hit(g, b, 0, "D>J", "dennis_D>J_whirlwind_kick", 80, after=90)
    hit(g, b, 0, "DvA", "dennis_DvA_many_foot", 50, after=80)
    reset(g)


# ── S6: hidden fighters (lf2.net) — Firzen x Julian ────────────────────────
def s6_firzen_julian(g):
    a, b = match(g, "Firzen", "Julian")
    hit(g, a, 1, "D>J", "firzen_D>J_ball", 160)
    hit(g, a, 1, "D^A", "firzen_D^A_disaster", 120, after=160)
    hit(g, a, 1, "D^J", "firzen_D^J_volcano", 40, after=120)
    hit(g, b, 0, "D>A", "julian_D>A_ball", 160)
    hit(g, b, 0, "D>J", "julian_D>J_big_ball", 180, after=100)
    hit(g, b, 0, "D^J", "julian_D^J_explosion", 40, after=110)
    hit(g, b, 0, "D^A", "julian_D^A_singlong", 35, after=100)
    reset(g)


# ── S7: strong vs weak hits — how far and how fast the victim flies ────────
def s7_knockback(g):
    a, b = match(g, "Davis", "Dennis")
    basic(g, a, 1, ["attack"] * 4, "kb_3_jabs_dizzy_then_super_punch", 30, gap=9, after=90)
    jump_attack(g, a, 1, "kb_jump_attack")
    basic(g, a, 1, ["run", "attack"], "kb_run_attack", 200, gap=2, after=80)
    dash_attack(g, a, 1, "kb_dash_attack")
    hit(g, a, 1, "D^A", "kb_dragon_punch", 35, after=110)
    hit(g, a, 1, "D>A", "kb_energy_blast", 160, after=80)
    hit(g, b, 0, "D>J", "kb_whirlwind_kick_on_davis", 80, after=100)
    # catch a dizzy opponent and throw him (forward + attack while catching)
    reset(g)
    g.mark("kb_catch_and_throw")
    make_dizzy_and_catch(g, a, 1)
    k = a.k[a.fwd()]
    g.down(k)
    a.tap("attack", 2, 2)
    g.up(k)
    for i in range(100):
        if i in (6, 20):
            g.shot("%05d_kb_catch_and_throw.ppm" % g.frame)
        g.step()
    # the thrown fighter used as a projectile against a third... (no third:
    # just the throw distance); then a guarded combo
    reset(g)
    b.approach(0, dx=30)
    g.mark("kb_defended_combo")
    g.down(P1["defend"])
    b.tap("attack", 2, 5); b.tap("attack", 2, 5); b.tap("attack", 2, 5)
    g.step(40)
    g.up(P1["defend"])
    g.step(40)
    reset(g)


# ── S8: 8-fighter chaos — AI, statuses crossing, weapons, clones, heals ─────
SPECIALS = {"Firzen": ["D>J", "D^A", "D^J"], "Julian": ["D>A", "D>J", "D^J", "D^A"]}


def enemies_of(g, me):
    return [o for o in g.objects() if o.type == 0 and o.idx != me.idx and o.team != me.team
            and o.hp > 0]


def brawl(g, pads, frames, specials):
    """each human pad: go to the nearest living enemy and alternate jabs and
    its special moves, deterministically, for `frames` frames"""
    end = g.frame + frames
    turn = 0
    next_shot = g.frame
    while g.frame < end:
        g.step()                     # always advance, even if nobody acts
        for pad, name in pads:
            me = pad.me()
            if me is None or me.hp <= 0:
                continue
            foes = enemies_of(g, me)
            if not foes:
                continue
            t = min(foes, key=lambda o: abs(o.x - me.x) + 2 * abs(o.z - me.z))
            pad.approach(t.idx, dx=60 if turn % 3 else 140, limit=60)
            if pad.me() is None:
                continue
            if turn % 3 == 0 and specials.get(name):
                seq = specials[name][(turn // 3) % len(specials[name])]
                g.mark("%s %s -> #%d id%d" % (name, seq, t.idx, t.id))
                pad.cmd(seq)
            else:
                for _ in range(3):
                    pad.tap("attack", 2, 9)
            g.step(25)
            turn += 1
        if g.frame >= next_shot:
            g.shot("%05d_brawl.ppm" % g.frame)
            next_shot = g.frame + 150


def s8_chaos(g):
    to_vs_select(g)
    pick(g, P1, "Firzen")
    pick(g, P2, "Julian")
    start_fight(g, bg_steps=2, computers=["Freeze", "Firen", "John", "Henry", "Rudolf", "Louis"])
    wait_fight(g)
    a, b = Pad(g, P1, 0), Pad(g, P2, 1)
    g.step(20)
    g.tap(VK_F[8], 2, 30)
    g.mark("chaos F8 weapons")
    brawl(g, [(a, "Firzen"), (b, "Julian")], 2400, SPECIALS)


# ── S9: Stage mode — enemies, bosses' AI, stage progression ────────────────
def s9_stage(g):
    to_vs_select(g, mode=1)
    pick(g, P1, "Davis")
    start_fight(g, bg_steps=0, easier=2)
    for _ in range(600):
        if any(o.type == 0 and o.x > 0 for o in g.objects()):
            break
        g.step()
    a = Pad(g, P1, 0)
    g.mark("stage 1 start")
    end = g.frame + 3000
    turn = 0
    next_shot = g.frame
    while g.frame < end:
        g.step()
        me = a.me()
        if me is None or me.hp <= 0:
            break
        if me.hp < 220:                      # F7 keeps the run going
            g.mark("davis hp %d: F7" % me.hp)
            g.tap(VK_F[7], 2, 10)
            continue
        foes = enemies_of(g, me)
        if not foes:
            k = a.k["right"]
            g.down(k)
            g.step(20)
            g.up(k)
            continue
        t = min(foes, key=lambda o: abs(o.x - me.x) + 2 * abs(o.z - me.z))
        a.approach(t.idx, dx=45 if turn % 4 else 150, limit=80)
        if turn % 4 == 0:
            a.cmd("D>A")
        else:
            for _ in range(4):
                a.tap("attack", 2, 9)
        g.step(10)
        turn += 1
        if g.frame >= next_shot:
            g.shot("%05d_stage.ppm" % g.frame)
            next_shot = g.frame + 200


SCENARIOS = {
    "s1_davis_firen": s1_davis_firen,
    "s2_freeze_woody": s2_freeze_woody,
    "s3_rudolf_john": s3_rudolf_john,
    "s4_louis_deep": s4_louis_deep,
    "s5_henry_dennis": s5_henry_dennis,
    "s6_firzen_julian": s6_firzen_julian,
    "s7_knockback": s7_knockback,
    "s8_chaos": s8_chaos,
    "s9_stage": s9_stage,
}
