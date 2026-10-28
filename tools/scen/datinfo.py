"""Decode an LF2 .dat and list what the frames and special inputs are."""
import re
import sys

KEY = b"SiuHungIsAGoodBearBecauseHeIsVeryGood"


def decode(path):
    d = open(path, "rb").read()
    return bytes((d[i] - KEY[i % 37]) & 0xFF for i in range(123, len(d))).decode("latin1")


def frames(text):
    out = {}
    for m in re.finditer(r"<frame>\s*(\d+)\s+(\S+)(.*?)<frame_end>", text, re.S):
        body = m.group(3)
        f = {"name": m.group(2)}
        for k in ("state", "next", "hit_a", "hit_d", "hit_j", "hit_Fa", "hit_Ua", "hit_Da",
                  "hit_Fj", "hit_Uj", "hit_Dj", "hit_ja", "mp"):
            mm = re.search(r"\b%s:\s*(-?\d+)" % k, body)
            if mm:
                f[k] = int(mm.group(1))
        f["opoint"] = [int(x) for x in re.findall(r"opoint:.*?oid:\s*(\d+)", body, re.S)]
        f["itr"] = re.findall(r"itr:(.*?)itr_end:", body, re.S)
        out[int(m.group(1))] = f
    return out


SPECIALS = {"hit_Fa": "D>A", "hit_Ua": "D^A", "hit_Da": "DvA", "hit_Fj": "D>J",
            "hit_Uj": "D^J", "hit_Dj": "DvJ", "hit_ja": "DJA"}

if __name__ == "__main__":
    fr = frames(decode(sys.argv[1]))
    st = fr[0]
    for k, n in SPECIALS.items():
        if st.get(k):
            t = st[k]
            print(n, t, fr.get(t, {}).get("name"), "mp", fr.get(t, {}).get("mp"))
