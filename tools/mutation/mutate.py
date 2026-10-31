#!/usr/bin/env python3
"""変異テスト: libs/<lib>/src/*.c の演算子を1か所ずつ壊し、そのライブラリのテストが失敗するかを数える。

    検出率 = テストが失敗した変異 / 有効な変異   (コンパイルできない変異と許可リストの同値変異は除く)

使い方:
    python tools/mutation/mutate.py --list                      # 対象ライブラリの一覧
    python tools/mutation/mutate.py embed_motion                # 1 ライブラリを測る
    python tools/mutation/mutate.py --all --check               # 全ライブラリを測り、baseline.json を下回れば失敗
    python tools/mutation/mutate.py embed_pid --update-baseline # 検出率を baseline.json に記録

リポジトリの作業ツリーは書き換えない。git 管理下のファイルを一時ディレクトリへコピーし、
そのコピーの上で 1 変異ずつ「書き換え → ビルド → ctest → 元に戻す」を繰り返す。
変異の抽出は --seed で固定されるので、同じソースなら同じ変異が選ばれる。
"""
from __future__ import annotations

import argparse
import json
import random
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BASELINE = HERE / "baseline.json"
ALLOWLIST = HERE / "equivalent.json"

# 演算子 -> 置換先。`-` `+` は二項演算子のときだけ対象にする (下の _is_binary 参照)。
SWAPS = {
    "==": "!=", "!=": "==",
    "<=": "<", ">=": ">",
    "<": "<=", ">": ">=",
    "&&": "||", "||": "&&",
    "+": "-", "-": "+",
}
TOKEN = re.compile(r"==|!=|<=|>=|&&|\|\||<<|>>|->|\+\+|--|\+=|-=|[<>+\-]")


@dataclass(frozen=True)
class Site:
    path: str   # リポジトリ相対 (/ 区切り)
    offset: int
    orig: str
    mut: str
    line_no: int
    line: str   # 前後の空白を除いた元の行 (許可リストの照合キー)

    @property
    def label(self) -> str:
        return f"{self.path}:{self.line_no}: {self.orig} -> {self.mut}   | {self.line}"


def mask(src: str) -> str:
    """コメント・文字列・文字リテラル・プリプロセッサ行を空白に置き換える (長さと改行は保つ)。"""
    out = list(src)
    i, n = 0, len(src)
    at_line_start = True
    while i < n:
        c = src[i]
        if c == "\n":
            at_line_start = True
            i += 1
            continue
        if at_line_start and c in " \t":
            i += 1
            continue
        if at_line_start and c == "#":
            # 行継続 (\) を含むプリプロセッサ行
            while i < n and not (src[i] == "\n" and src[i - 1] != "\\"):
                if src[i] != "\n":
                    out[i] = " "
                i += 1
            continue
        at_line_start = False
        if src.startswith("//", i):
            while i < n and src[i] != "\n":
                out[i] = " "
                i += 1
        elif src.startswith("/*", i):
            end = src.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for k in range(i, end):
                if src[k] != "\n":
                    out[k] = " "
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            for k in range(i, min(j + 1, n)):
                if src[k] != "\n":
                    out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def _is_binary(masked: str, pos: int) -> bool:
    """`+` `-` が二項演算子か (直前の非空白が値で終わっているか)。"""
    j = pos - 1
    while j >= 0 and masked[j] in " \t\n":
        j -= 1
    return j >= 0 and (masked[j].isalnum() or masked[j] in "_)]")


def find_sites(path: Path, rel: str) -> list[Site]:
    src = path.read_text(encoding="utf-8")
    masked = mask(src)
    lines = src.split("\n")
    starts = [0]
    for ln in lines[:-1]:
        starts.append(starts[-1] + len(ln) + 1)
    sites = []
    for m in TOKEN.finditer(masked):
        tok = m.group()
        if tok not in SWAPS:
            continue
        if tok in "+-" and not _is_binary(masked, m.start()):
            continue
        line_no = max(i for i, s in enumerate(starts) if s <= m.start()) + 1
        sites.append(Site(rel, m.start(), tok, SWAPS[tok], line_no, lines[line_no - 1].strip()))
    return sites


def load_allowlist() -> list[dict]:
    if not ALLOWLIST.exists():
        return []
    return json.loads(ALLOWLIST.read_text(encoding="utf-8"))


def is_equivalent(site: Site, allow: list[dict]) -> bool:
    return any(a["file"] == site.path and a["line"] == site.line and a["change"] == f"{site.orig} -> {site.mut}"
               for a in allow)


def run(cmd: list[str], cwd: Path, timeout: int | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout, encoding="utf-8",
                          errors="replace")


def snapshot(dest: Path) -> None:
    """git 管理下のファイルだけを dest にコピーする (build 成果物や未追跡ファイルは持ち込まない)。"""
    files = run(["git", "ls-files"], ROOT).stdout.splitlines()
    for f in files:
        src = ROOT / f
        if src.is_file():
            (dest / f).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dest / f)


def libraries() -> list[str]:
    return sorted(p.name for p in (ROOT / "libs").iterdir() if (p / "src").is_dir() and any((p / "src").glob("*.c")))


class Workspace:
    """ソースのコピーと専用ビルドディレクトリ。1 ワーカーが 1 つ使う。"""

    def __init__(self, base: Path, name: str, config: str, generator: str | None):
        self.src = base / name / "src"
        self.bld = base / name / "build"
        self.config = config
        self.src.mkdir(parents=True)
        snapshot(self.src)
        cmd = ["cmake", "-S", str(self.src), "-B", str(self.bld), "-DBUILD_APPS=OFF",
               f"-DCMAKE_BUILD_TYPE={config}"]
        if generator:
            cmd += ["-G", generator]
        r = run(cmd, ROOT)
        if r.returncode != 0:
            sys.exit(f"cmake の構成に失敗しました:\n{r.stdout}\n{r.stderr}")

    def tests_of(self, lib: str) -> list[tuple[str, str]]:
        """ライブラリ配下の (ctest 名, ビルドターゲット名)。ctest の作業ディレクトリが libs/<lib>/ 以下のもの。"""
        r = run(["ctest", "--test-dir", str(self.bld), "-C", self.config, "--show-only=json-v1"], ROOT)
        found = []
        for t in json.loads(r.stdout)["tests"]:
            wd = next((p["value"] for p in t.get("properties", []) if p["name"] == "WORKING_DIRECTORY"), "")
            if f"/libs/{lib}/" in wd.replace("\\", "/") + "/":
                # command は未ビルドだと出力されない。その場合は ctest 名 = ターゲット名 (本リポジトリの規約) とみなす
                target = Path(t["command"][0]).stem if t.get("command") else t["name"]
                found.append((t["name"], target))
        return found

    def build(self, targets: list[str]) -> bool:
        cmd = ["cmake", "--build", str(self.bld), "--config", self.config, "--parallel", "4", "--target", *targets]
        return run(cmd, ROOT).returncode == 0

    def test(self, names: list[str]) -> bool:
        regex = "^(" + "|".join(re.escape(n) for n in names) + ")$"
        r = run(["ctest", "--test-dir", str(self.bld), "-C", self.config, "-R", regex, "--timeout", "30"], ROOT)
        return r.returncode == 0


def evaluate(ws: Workspace, site: Site, targets: list[str], names: list[str]) -> str:
    """'killed' (テストが失敗) / 'survived' (全テスト通過) / 'invalid' (コンパイル不能)。"""
    f = ws.src / site.path
    original = f.read_text(encoding="utf-8")
    try:
        mutated = original[:site.offset] + site.mut + original[site.offset + len(site.orig):]
        f.write_text(mutated, encoding="utf-8", newline="")
        if not ws.build(targets):
            return "invalid"
        return "survived" if ws.test(names) else "killed"
    finally:
        f.write_text(original, encoding="utf-8", newline="")


def measure(lib: str, args, base: Path) -> dict:
    allow = load_allowlist()
    sites: list[Site] = []
    for f in sorted((ROOT / "libs" / lib / "src").glob("*.c")):
        sites += find_sites(f, f.relative_to(ROOT).as_posix())
    skipped = [s for s in sites if is_equivalent(s, allow)]
    candidates = [s for s in sites if not is_equivalent(s, allow)]
    random.Random(f"{args.seed}:{lib}").shuffle(candidates)
    picked = candidates[:args.count]

    # 作業コピーを並列数ぶん用意する (各ワーカーは自分のコピーだけを書き換える)
    workers = [Workspace(base, f"{lib}-{i}", args.config, args.generator) for i in range(args.jobs)]
    tests = workers[0].tests_of(lib)
    if not tests:
        sys.exit(f"{lib}: ctest に登録されたテストが見つかりません")
    names, targets = [n for n, _ in tests], sorted({t for _, t in tests})
    for w in workers:  # 未変異で一度ビルドし、通ることを確認する
        if not w.build(targets) or not w.test(names):
            sys.exit(f"{lib}: 変異前のビルドまたはテストが失敗しています")

    free = list(workers)

    lock = threading.Lock()

    def one(site: Site):
        with lock:
            w = free.pop()
        try:
            return site, evaluate(w, site, targets, names)
        finally:
            with lock:
                free.append(w)

    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for site, status in pool.map(one, picked):
            results.append((site, status))
            if args.verbose or status == "survived":
                print(f"  [{status:8}] {site.label}", flush=True)

    killed = sum(1 for _, s in results if s == "killed")
    valid = sum(1 for _, s in results if s != "invalid")
    rate = killed / valid if valid else 1.0
    print(f"{lib}: 検出率 {rate:.0%} ({killed}/{valid}) 試行 {len(picked)} 無効 {len(picked) - valid}"
          f" 同値除外 {len(skipped)} 全候補 {len(sites)}", flush=True)
    return {"rate": round(rate, 4), "killed": killed, "valid": valid}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("libs", nargs="*", help="対象ライブラリ (libs/ 配下の名前)")
    ap.add_argument("--all", action="store_true", help="全ライブラリを対象にする")
    ap.add_argument("--list", action="store_true", help="対象ライブラリを表示して終了")
    ap.add_argument("--count", type=int, default=80, help="ライブラリごとに試す変異の数 (既定 80)")
    ap.add_argument("--seed", default="c_cpp_lib", help="変異の抽出に使う種 (既定固定)")
    ap.add_argument("--jobs", type=int, default=4, help="並列数 (作業コピーとビルドディレクトリを並列数ぶん作る)")
    ap.add_argument("--config", default="Debug", help="ビルド構成 (既定 Debug)")
    ap.add_argument("-G", "--generator", help="CMake ジェネレータ (省略時は CMake の既定)")
    ap.add_argument("--check", action="store_true", help="baseline.json を許容差より下回ったら終了コード 1")
    ap.add_argument("--tolerance", type=float, default=0.05, help="--check の許容差 (既定 0.05 = 5 ポイント)")
    ap.add_argument("--update-baseline", action="store_true", help="測定結果を baseline.json に書き込む")
    ap.add_argument("-v", "--verbose", action="store_true", help="検出された変異も表示する")
    args = ap.parse_args()

    if args.list:
        print("\n".join(libraries()))
        return 0
    targets = libraries() if args.all else args.libs
    if not targets:
        ap.error("ライブラリ名か --all を指定してください")
    unknown = [t for t in targets if t not in libraries()]
    if unknown:
        ap.error(f"不明なライブラリ: {', '.join(unknown)}")

    baseline = json.loads(BASELINE.read_text(encoding="utf-8")) if BASELINE.exists() else {}
    failed, measured = [], {}
    with tempfile.TemporaryDirectory(prefix="mutation-") as tmp:
        for lib in targets:
            print(f"== {lib}", flush=True)
            measured[lib] = measure(lib, args, Path(tmp) / lib)
            if args.check:
                floor = baseline.get(lib, {}).get("rate")
                if floor is None:
                    print(f"  baseline.json に {lib} がありません")
                    failed.append(lib)
                elif measured[lib]["rate"] + args.tolerance < floor:
                    print(f"  最低ライン {floor:.0%} (許容差 {args.tolerance:.0%}) を下回りました")
                    failed.append(lib)

    if args.update_baseline:
        baseline.update(measured)
        BASELINE.write_text(json.dumps(dict(sorted(baseline.items())), indent=2, ensure_ascii=False) + "\n",
                            encoding="utf-8")
        print(f"{BASELINE.relative_to(ROOT)} を更新しました")
    if failed:
        print("最低ラインを満たさないライブラリ: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
