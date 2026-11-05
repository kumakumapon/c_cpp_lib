#!/usr/bin/env python3
"""embed_crc のルックアップテーブル (src/crc_tables.h) を生成する。

    python libs/embed_crc/tools/gen_crc_tables.py > libs/embed_crc/src/crc_tables.h

テーブルは「1 バイト分のシフト結果」を保持する。反転 (reflected) 方式は右シフト、
非反転方式は左シフトで作る。同じ多項式・方向のアルゴリズムは 1 つのテーブルを共有する。
"""

TABLES = [
    # (C 名, 幅, 正規形の多項式, reflected)
    ("crc_t8_1d", 8, 0x1D, False),        # CRC-8/SAE-J1850
    ("crc_t8_07", 8, 0x07, False),        # CRC-8/SMBUS
    ("crc_t8_31", 8, 0x31, False),        # CRC-8/NRSC-5 (Sensirion)
    ("crc_t8_31r", 8, 0x31, True),        # CRC-8/MAXIM-DOW (1-Wire)
    ("crc_t16_1021", 16, 0x1021, False),  # CRC-16/IBM-3740 (CCITT-FALSE), XMODEM
    ("crc_t16_8005r", 16, 0x8005, True),  # CRC-16/MODBUS
    ("crc_t16_1021r", 16, 0x1021, True),  # CRC-16/KERMIT, IBM-SDLC (X.25)
    ("crc_t32_04c11db7", 32, 0x04C11DB7, False),  # CRC-32/MPEG-2
    ("crc_t32_04c11db7r", 32, 0x04C11DB7, True),  # CRC-32/ISO-HDLC
    ("crc_t32_1edc6f41r", 32, 0x1EDC6F41, True),  # CRC-32/ISCSI (Castagnoli)
]


def reflect(v: int, w: int) -> int:
    return int(f"{v:0{w}b}"[::-1], 2)


def table(w: int, poly: int, refl: bool) -> list[int]:
    mask = (1 << w) - 1
    out = []
    for i in range(256):
        if refl:
            rp = reflect(poly, w)
            r = i
            for _ in range(8):
                r = (r >> 1) ^ rp if r & 1 else r >> 1
        else:
            r = i << (w - 8)
            for _ in range(8):
                r = ((r << 1) ^ poly) if r & (1 << (w - 1)) else (r << 1)
                r &= mask
        out.append(r & mask)
    return out


def main() -> None:
    ctype = {8: "uint8_t", 16: "uint16_t", 32: "uint32_t"}
    digits = {8: 2, 16: 4, 32: 8}
    print("/* 自動生成: libs/embed_crc/tools/gen_crc_tables.py。手で編集しないこと。 */")
    print("#ifndef EMBED_CRC_TABLES_H")
    print("#define EMBED_CRC_TABLES_H")
    print("#ifdef EMBED_CRC_TABLES")
    print("#include <stdint.h>")
    for name, w, poly, refl in TABLES:
        vals = table(w, poly, refl)
        per = 12 if w == 8 else (10 if w == 16 else 7)
        print(f"static const {ctype[w]} {name}[256] = {{")
        for i in range(0, 256, per):
            row = ", ".join(f"0x{v:0{digits[w]}x}u" for v in vals[i:i + per])
            print(f"    {row},")
        print("};")
    print("#endif /* EMBED_CRC_TABLES */")
    print("#endif /* EMBED_CRC_TABLES_H */")


if __name__ == "__main__":
    main()
