"""Guard: refuse files that are, or look like, game data.

    python tools/guard.py          the staged files (the pre-commit hook)
    python tools/guard.py --all    every tracked file (CI)

Checks each file for:
- a SHA-1 equal to a file of the original package (hashes below),
- a leading magic of one of the original formats, or of what the extractor
  writes (decoded sound),
- a PNG anywhere but res/ (the project's own icons; screenshots of the game
  stay out),
- a size over 1 MiB (nothing we write should be that large).
"""
import hashlib
import subprocess
import sys

# SHA-1 of the XBLA package and of every file inside it (title 584108FF, v0x102).
KNOWN_SHA1 = {
    "E39F01210CE4A2DA13AFEA3F338F642373B31E63",  # the STFS package
    "F9AD672718538FDB5CDA1688691F349F5FE5EE8A", "115CA9880035BE37F4ABA95F21221BEC1D049D91",
    "EDFCCFEF21DC272832AB18E08EA62AD78A53107F", "28CEBAC887E92DBDC87C32B7950F7EF26EA003DF",
    "04172E21752B41E5F884647A527FF2DBDDD3FAE9", "45C2002BEA100B368A8498829B1AD8F7AEB0BB7A",
    "D2D220E8BDA6EB6A497420D2702E1CAA5407A3B4", "9D44E3F91C31989ABB43E0A0DFE37A50DC121DDB",
    "DA533311E43082E228FCEDC55621F16005AC7381", "EB79031CABE210E1D57C0D0B1B9FF7ACE123B0BA",
    "BC0C57C887055507E39FD0F80327FE49FABC79B4", "0BBE096D5978CF2E3B6CE53ED76A643A64316C34",
    "E5AFB42965EBF98BA770E7CBAE8E3EA08DBB60F2", "B5A15C6E507227B83E62EC723488D4A9ABC68DAD",
    "C34A54FD97FD1637CE488E149DBD872CFE269424", "60B297A57AA69623DB78C0C497884F2F74A401C4",
    "048B852E52A8C776C7AE752141591701B7EA5E69", "23F73C5468680EB2BC3B88BC35E362ECA8E95FE7",
    "E6DEA08C25635D523BAEA23A45895ECE82E1778D", "126FF9CA5160EF483E54A21E82F6FE986CC6603C",
    "8CDC55AA520DE01210CC904C402C9121A6DD48BE", "584708EA0C5CFEAAC4A0B83CB4057A9BC88967EA",
    "56227B6625883BF65351AC529C11A8C1914F0C30", "3BC7638F26104B0B2C543A267A1E5B6A9B88631D",
    "C7354ECFED5F44905F54E698DE5373A7B2021341", "751152ACD3EA776D522157A20596958889CD0DC2",
    "D5315019762C20A7E6568CE1711AC886DDE742B9", "467B9E93F3FA9FFF54BB9431F6E3960B241A22F3",
    "DEF1D8DEA566877AF7CB2041879C8B42C389F171", "230D46030D5E4D0E1B29F3DF756A61651052264B",
    "CB2DCF68BF5259F1EA7CFF0BAB3FDDD1576A329E", "54317EA8CAF98804E0411A6BE159A593B22F596C",
    "15A284E9A0618913C434A4A3AB6FFEE60C3B83FB", "92ED9B6E754071479AC06DD4ED7AC0D878FDCB6C",
    "D220B9815C4364034B0A0810137069DFB7FA5430", "D8738A896621F45EAD871D50015E0BECD00E3FF8",
    "A7175F37722481213DA3251FC0720DCF1C196EE6", "410F6497DB045908585FAE74CF2FD435E3066ACA",
    "77507607EF2B10663A80DFB4EDCC8F6A40E8D199", "A133D8FBFD24E3D3AF1DC99CE9010149CAE7B2C1",
    "2ECB2E7DE6EAE5575244704CA9CC15F4C643F56B", "A990084F0F25E6E86C7ED984849283ECACA6D967",
    "635B7A0F6D3AF77869ADFB6328BDC817B917A1C2", "1B4CDE2C8B0285D075BCB7E1CBEED6D06D878875",
    "D8D3F6ECBF54F03840BC67B0D790B555D64F5877", "C9FE9D2EF73C512AC52451C298FE15E460636A20",
    "B61B15F5BB504EFB412EC4AAFC83675ABCDF20C8", "C01921C369C25E317BAA2B32272131CE04387A3F",
    "F702142FDE0BCC6607E4E9507BB00D2AD4ADF124", "C70591704363E25ED97707DA4E4D763C5BC9B826",
    "56E4EA66BB47B672ABA2C80F5046211D5AFC66FD", "9C3D960BFA0DAB85AC16AD189B922AB89588030E",
    "A43E38F1F90AA1018BA15DA5AB1E8045A396D3E1",
}

# Leading magics of the original formats, of a decrypted XEX's PE image and
# of the extractor's decoded sound (WAV).
MAGICS = [b"LIVE", b"CON ", b"PIRS", b"XEX2", b"XTLO", b"FLSK", b"BANK", b"AOBJ", b"MZ", b"RIFF"]
PNG = b"\x89PNG"

MAX_SIZE = 1 << 20


def staged_files():
    out = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"],
        capture_output=True, check=True,
    ).stdout
    return [p for p in out.decode("utf-8").split("\0") if p]


def staged_blob(path):
    return subprocess.run(["git", "show", f":{path}"], capture_output=True, check=True).stdout


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], capture_output=True, check=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p]


def tracked_blob(path):
    with open(path, "rb") as f:
        return f.read()


def main():
    every = "--all" in sys.argv[1:]
    bad = []
    for path in tracked_files() if every else staged_files():
        data = tracked_blob(path) if every else staged_blob(path)
        if hashlib.sha1(data).hexdigest().upper() in KNOWN_SHA1:
            bad.append(f"{path}: identical to a file of the original game")
        elif any(data.startswith(m) for m in MAGICS):
            bad.append(f"{path}: starts with the magic of an original game format or of extracted data")
        elif data.startswith(PNG) and not path.startswith("res/"):
            bad.append(f"{path}: a PNG outside res/ (no screenshots or game images)")
        elif len(data) > MAX_SIZE:
            bad.append(f"{path}: larger than 1 MiB")
    for b in bad:
        print("guard:", b, file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
