#!/usr/bin/python3 -I
"""Write the Windows test files of tests/data/windows, made from nothing.

    tools/make-windows-fixtures.py [OUTDIR]

Every byte comes from this script: small programs with icons, version
details and a signature block, shortcuts and registry files the way Wine
writes them. No file made by Microsoft or anyone else is copied. The
signature is only well formed, not valid: the tests read the publisher
name from it, and checking it is osslsigncode's job.

The output is the same on every run, so the files in the repo can be
compared with a fresh run.
"""

import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "tests", "data", "windows")

VIOLET = (0x7B, 0x3C, 0xFF, 0xFF)
MAGENTA = (0xC0, 0x40, 0xFF, 0xFF)
GREEN = (0x20, 0xC0, 0x60, 0xFF)
BLUE = (0x30, 0x60, 0xE0, 0xFF)
RED = (0xE0, 0x30, 0x30, 0xFF)


def u16(v):
    return struct.pack("<H", v)


def u32(v):
    return struct.pack("<I", v)


def align(data, n):
    return data + b"\0" * (-len(data) % n)


# --- pictures ---------------------------------------------------------------

def pixels(size, colour, ring=None):
    """size x size RGBA rows: a filled square, a ring of another colour
    drawn inside so the picture is not flat."""
    rows = []
    c = (size - 1) / 2
    for y in range(size):
        row = []
        for x in range(size):
            d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            if ring and size * 0.25 <= d <= size * 0.35:
                row.append(ring)
            else:
                row.append(colour)
        rows.append(row)
    return rows


def png(size, colour, ring=None):
    raw = b"".join(b"\0" + b"".join(bytes(p) for p in row)
                   for row in pixels(size, colour, ring))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def dib(size, colour):
    """A 32-bit icon bitmap as RT_ICON holds it: header, bottom-up BGRA rows,
    then the AND mask (all zero: the alpha channel decides)."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    body = b"".join(b"".join(bytes((p[2], p[1], p[0], p[3])) for p in row)
                    for row in reversed(pixels(size, colour)))
    mask_row = (size + 31) // 32 * 4
    return header + body + b"\0" * (mask_row * size)


def ico_file(images):
    """An .ico file from (width, data, bitcount) entries."""
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    blobs = b""
    for width, data, bits in images:
        out += struct.pack("<BBBBHHII", width % 256, width % 256, 0, 0, 1, bits,
                           len(data), offset + len(blobs))
        blobs += data
    return out + blobs


# --- DER, just enough for a PKCS#7 signature block ------------------------

def der(tag, body):
    n = len(body)
    if n < 0x80:
        length = bytes([n])
    else:
        b = n.to_bytes((n.bit_length() + 7) // 8, "big")
        length = bytes([0x80 | len(b)]) + b
    return bytes([tag]) + length + body


def seq(*parts):
    return der(0x30, b"".join(parts))


def set_(*parts):
    return der(0x31, b"".join(parts))


def oid(text):
    parts = [int(p) for p in text.split(".")]
    body = bytes([parts[0] * 40 + parts[1]])
    for p in parts[2:]:
        chunk = [p & 0x7F]
        p >>= 7
        while p:
            chunk.append(0x80 | (p & 0x7F))
            p >>= 7
        body += bytes(reversed(chunk))
    return der(0x06, body)


def integer(v):
    return der(0x02, v.to_bytes((v.bit_length() + 8) // 8, "big"))


def name(*rdns):
    """A distinguished name from (oid, text, string tag) triples."""
    return seq(*(set_(seq(oid(o), der(tag, text.encode("utf-8" if tag != 0x1E else "utf-16-be"))))
                 for o, text, tag in rdns))


CN, O, C = "2.5.4.3", "2.5.4.10", "2.5.4.6"
SHA256 = seq(oid("2.16.840.1.101.3.4.2.1"), der(0x05, b""))
RSA_SHA256 = seq(oid("1.2.840.113549.1.1.11"), der(0x05, b""))


def certificate(serial, issuer, subject):
    tbs = seq(der(0xA0, integer(2)), integer(serial), RSA_SHA256, issuer,
              seq(der(0x17, b"260101000000Z"), der(0x17, b"360101000000Z")),
              subject,
              seq(seq(oid("1.2.840.113549.1.1.1"), der(0x05, b"")), der(0x03, b"\0" + b"\x01" * 16)))
    return seq(tbs, RSA_SHA256, der(0x03, b"\0" + b"\x02" * 16))


def signed_data(publisher):
    root = name((C, "CA", 0x13), (CN, "Raye Test Root", 0x0C))
    leaf = name((C, "CA", 0x13), (O, "Raye Test Org", 0x13), (CN, publisher, 0x0C))
    # the issuer's certificate first: the signer is found by issuer and serial
    certs = certificate(1, root, root) + certificate(0x1234, root, leaf)
    signer = seq(integer(1), seq(root, integer(0x1234)), SHA256,
                 seq(oid("1.2.840.113549.1.9.3")), der(0x04, b"\x03" * 32))
    content = seq(integer(1), set_(SHA256),
                  seq(oid("1.3.6.1.4.1.311.2.1.4"), der(0xA0, seq(der(0x04, b"\x04" * 32)))),
                  der(0xA0, certs), set_(signer))
    return seq(oid("1.2.840.113549.1.7.2"), der(0xA0, content))


# --- resources ----------------------------------------------------------

def wstr(text):
    return text.encode("utf-16-le") + b"\0\0"


def vs_node(key, value=b"", children=(), text=False):
    """One node of a VS_VERSIONINFO tree: length, value length, type, key,
    then the value and the children, each on a 4-byte boundary."""
    body = b"\0" * 6 + wstr(key)
    body = align(body, 4) + value
    for child in children:
        body = align(body, 4) + child
    value_len = len(value) // 2 if text else len(value)
    return struct.pack("<HHH", len(body), value_len, 1 if text else 0) + body[6:]


def version_resource(tables, file_version=(1, 2, 3, 4)):
    ms = (file_version[0] << 16) | file_version[1]
    ls = (file_version[2] << 16) | file_version[3]
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x00010000, ms, ls, ms, ls, 0x3F, 0, 0x40004, 1, 0, 0, 0)
    string_tables = []
    for lang, strings in tables:
        string_tables.append(vs_node(lang, children=[
            vs_node(k, wstr(v), text=True) for k, v in strings]))
    var = vs_node("VarFileInfo", children=[
        vs_node("Translation", struct.pack("<HH", 0x0409, 0x04B0))])
    return vs_node("VS_VERSION_INFO", fixed,
                   [vs_node("StringFileInfo", children=string_tables), var])


def resource_section(resources, rva):
    """resources: {type id: {name id: data}} with language 0409 leaves.
    Laid out as the linker does: directories, data entries, then data."""
    types = sorted(resources)
    dirs = b""
    # sizes first, to know where the data entries and the data go
    n_names = sum(len(resources[t]) for t in types)
    dir_size = (16 + 8 * len(types)) + sum(16 + 8 * len(resources[t]) for t in types) \
        + n_names * (16 + 8)
    entries_at = dir_size
    data_at = entries_at + 16 * n_names
    blobs = b""
    data_entries = b""
    type_dir = struct.pack("<IIHHHH", 0, 0, 0, 0, 0, len(types))
    name_dirs, lang_dirs = b"", b""
    name_dir_at = 16 + 8 * len(types)
    lang_dir_at = name_dir_at + sum(16 + 8 * len(resources[t]) for t in types)
    leaf = 0
    for t in types:
        names = sorted(resources[t])
        type_dir += struct.pack("<II", t, 0x80000000 | (name_dir_at + len(name_dirs)))
        nd = struct.pack("<IIHHHH", 0, 0, 0, 0, 0, len(names))
        for n in names:
            nd += struct.pack("<II", n, 0x80000000 | (lang_dir_at + len(lang_dirs)))
            lang_dirs += struct.pack("<IIHHHH", 0, 0, 0, 0, 0, 1) + struct.pack(
                "<II", 0x0409, entries_at + 16 * leaf)
            data = resources[t][n]
            data_entries += struct.pack("<IIII", rva + data_at + len(blobs), len(data), 0, 0)
            blobs = align(blobs + data, 8)
            leaf += 1
        name_dirs += nd
    dirs = type_dir + name_dirs + lang_dirs
    assert len(dirs) == dir_size, (len(dirs), dir_size)
    return dirs + data_entries + blobs


def icon_resources(groups, first_icon_id=1):
    """groups: {group id: [(size, data, bitcount)]} -> RT_ICON + RT_GROUP_ICON."""
    icons, group_res = {}, {}
    next_id = first_icon_id
    for gid in sorted(groups):
        entries = b""
        for size, data, bits in groups[gid]:
            icons[next_id] = data
            entries += struct.pack("<BBBBHHIH", size % 256, size % 256, 0, 0, 1, bits,
                                   len(data), next_id)
            next_id += 1
        group_res[gid] = struct.pack("<HHH", 0, 1, len(groups[gid])) + entries
    return {3: icons, 14: group_res}


def pe(resources, is64=True, signature=None):
    rva = 0x1000
    rsrc = resource_section(resources, rva) if resources else b""
    raw = align(rsrc, 0x200)
    opt_size = 240 if is64 else 224
    dos = bytearray(0x80)
    dos[0:2] = b"MZ"
    dos[0x3C:0x40] = u32(0x80)
    coff = struct.pack("<HHIIIHH", 0x8664 if is64 else 0x14C, 1, 0, 0, 0, opt_size,
                       0x0022 if is64 else 0x0102)
    size_of_image = rva + max(0x1000, (len(rsrc) + 0xFFF) // 0x1000 * 0x1000)
    if is64:
        opt = struct.pack("<HBBIIIII", 0x20B, 14, 0, 0, len(raw), 0, 0, rva)
        opt += struct.pack("<Q", 0x140000000)
    else:
        opt = struct.pack("<HBBIIIIII", 0x10B, 14, 0, 0, len(raw), 0, 0, rva, 0)
        opt += struct.pack("<I", 0x400000)
    opt += struct.pack("<IIHHHHHHIIIIHH", 0x1000, 0x200, 6, 0, 0, 0, 6, 0, 0,
                       size_of_image, 0x200, 0, 2, 0x8160)
    stack = struct.pack("<QQQQ", 0x100000, 0x1000, 0x100000, 0x1000) if is64 \
        else struct.pack("<IIII", 0x100000, 0x1000, 0x100000, 0x1000)
    opt += stack + struct.pack("<II", 0, 16)
    dirs = [(0, 0)] * 16
    if rsrc:
        dirs[2] = (rva, len(rsrc))
    body_end = 0x200 + len(raw)
    cert = b""
    if signature is not None:
        win_cert = struct.pack("<IHH", 8 + len(signature), 0x0200, 0x0002) + signature
        cert = align(win_cert, 8)
        dirs[4] = (body_end, len(cert))
    opt += b"".join(struct.pack("<II", a, s) for a, s in dirs)
    assert len(opt) == opt_size
    section = struct.pack("<8sIIIIIIHHI", b".rsrc", max(len(rsrc), 1), rva, len(raw), 0x200,
                          0, 0, 0, 0, 0x40000040)
    headers = align(bytes(dos) + b"PE\0\0" + coff + opt + section, 0x200)
    return headers + raw + cert


# --- shortcuts (MS-SHLLINK) ---------------------------------------------

LINK_CLSID = bytes.fromhex("0114020000000000c000000000000046")
MY_COMPUTER = bytes.fromhex("e04fd020ea3a6910a2d808002b30309d")


def lnk_string(text, unicode):
    if unicode:
        return u16(len(text)) + text.encode("utf-16-le")
    data = text.encode("latin-1")
    return u16(len(data)) + data


def link_info(path, unicode_path=False):
    base, suffix = path, ""
    volume = u32(16) + u32(3) + u32(0x1234ABCD) + u32(16) + b"\0"  # fixed disk, empty label
    volume = u32(len(volume) + 4) + volume[4:]
    header_size = 0x24 if unicode_path else 0x1C
    ansi = base.encode("latin-1", "replace") + b"\0"
    off_volume = header_size
    off_base = off_volume + len(volume)
    off_suffix = off_base + len(ansi)
    body = volume + ansi + suffix.encode("latin-1") + b"\0"
    extra = b""
    if unicode_path:
        off_base_u = header_size + len(body)
        uni = base.encode("utf-16-le") + b"\0\0"
        off_suffix_u = off_base_u + len(uni)
        body += uni + b"\0\0"
        extra = u32(off_base_u) + u32(off_suffix_u)
    head = u32(header_size) + u32(1) + u32(off_volume) + u32(off_base) + u32(0) + u32(off_suffix) + extra
    total = 4 + len(head) + len(body)
    return u32(total) + head + body


def id_list(path):
    """Shell items for C:\\a\\b\\c.exe: My Computer, the drive, then one
    file entry per name, each with its long Unicode name in a version 9
    extension block."""
    items = [b"\x1f\x50" + MY_COMPUTER]
    drive, *names = path.split("\\")
    items.append(b"\x2f" + (drive + "\\").encode("ascii").ljust(22, b"\0"))
    for i, n in enumerate(names):
        kind = 0x32 if i == len(names) - 1 else 0x31
        short = align(n.encode("ascii", "replace")[:12] + b"\0", 2)
        long_name = n.encode("utf-16-le") + b"\0\0"
        block = (u16(9) + u32(0xBEEF0004) + u32(0) + u32(0) + u16(0x2E)
                 + u16(0) + b"\0" * 8 + b"\0" * 8 + u32(0)
                 + long_name + u16(0))
        block = u16(len(block) + 4) + block + u16(4 + 2 + len(short) + 12)
        items.append(bytes([kind, 0]) + u32(0) + u32(0) + u16(0x20) + short + block)
    body = b"".join(u16(len(i) + 2) + i for i in items) + u16(0)
    return u16(len(body)) + body


def lnk(target=None, name=None, args=None, workdir=None, icon=None, icon_index=0,
        unicode=True, unicode_path=False, env=None, idlist=None, relative=None):
    flags = 0
    if idlist:
        flags |= 0x01
    if target:
        flags |= 0x02
    if name is not None:
        flags |= 0x04
    if relative is not None:
        flags |= 0x08
    if workdir is not None:
        flags |= 0x10
    if args is not None:
        flags |= 0x20
    if icon is not None:
        flags |= 0x40
    if unicode:
        flags |= 0x80
    if env:
        flags |= 0x200
    header = u32(0x4C) + LINK_CLSID + u32(flags) + u32(0x20) + b"\0" * 24 + u32(0) \
        + struct.pack("<i", icon_index) + u32(1) + u16(0) + b"\0" * 10
    assert len(header) == 0x4C
    out = header
    if idlist:
        out += id_list(idlist)
    if target:
        out += link_info(target, unicode_path)
    for value in (name, relative, workdir, args, icon):
        if value is not None:
            out += lnk_string(value, unicode)
    if env:
        ansi = env.encode("latin-1", "replace")[:259].ljust(260, b"\0")
        uni = env.encode("utf-16-le")[:518].ljust(520, b"\0")
        out += u32(0x314) + u32(0xA0000001) + ansi + uni
    return out + u32(0)


# --- registry files, as Wine writes them --------------------------------

def reg_escape(text):
    out = []
    for i, ch in enumerate(text):
        code = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif code > 127 or code < 32:
            nxt = text[i + 1] if i + 1 < len(text) else ""
            fmt = "\\x%04x" if nxt and nxt in "0123456789abcdefABCDEF" else "\\x%x"
            out.append(fmt % code)
        else:
            out.append(ch)
    return "".join(out)


def reg_key(path, stamp, values):
    lines = [f"[{reg_escape(path)}] {stamp}", f"#time={stamp:x}00000000"]
    for k, v in values:
        key = "@" if k is None else f'"{reg_escape(k)}"'
        if isinstance(v, int):
            lines.append(f"{key}=dword:{v:08x}")
        elif isinstance(v, tuple):
            kind, text = v
            lines.append(f'{key}={kind}:"{reg_escape(text)}"')
        else:
            lines.append(f'{key}="{reg_escape(v)}"')
    return "\n".join(lines) + "\n"


UNINSTALL = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
UNINSTALL32 = "Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
HEAD = "WINE REGISTRY Version 2\n;; All keys relative to \\\\Machine\n\n#arch=win64\n\n"

TEMPLATE_KEYS = [
    reg_key(UNINSTALL + "{0A4A6B47-9B7E-46A2-AC7E-1E4A3D6D2C3B}", 1793000000, [
        ("DisplayName", "Wine Mono Runtime"), ("Publisher", "The Wine Project"),
        ("DisplayVersion", "10.4.0"), ("WindowsInstaller", 1)]),
    reg_key(UNINSTALL32 + "{5B1E1C0F-7A2D-4E47-9C3B-0C8F3E8E7A10}", 1793000000, [
        ("DisplayName", "Runtime Pieces x86"), ("Publisher", "Someone"),
        ("DisplayVersion", "14.40"), ("SystemComponent", 0)]),
]


def system_reg():
    keys = list(TEMPLATE_KEYS) + [
        reg_key(UNINSTALL + "RayeTestTool", 1793300000, [
            ("DisplayName", "Raye Test Tool"), ("Publisher", "Raye Test Publisher"),
            ("DisplayVersion", "1.2.3"), ("DisplayIcon", "C:\\Program Files\\Raye Test\\tool.exe,0"),
            ("InstallLocation", "C:\\Program Files\\Raye Test"),
            ("UninstallString", '"C:\\Program Files\\Raye Test\\unins000.exe"'),
            ("InstallDate", "20261029"), ("EstimatedSize", 2048)]),
        reg_key(UNINSTALL32 + "Unicode Tool_is1", 1793310000, [
            ("DisplayName", "Ünïcødé 工具 1.0"), ("Publisher", "Ça Marche Ltd"),
            ("DisplayVersion", "1.0"),
            ("InstallLocation", ("str(2)", "%ProgramFiles%\\Ünïcødé 工具\\")),
            ("UninstallString", '"C:\\Program Files\\Ünïcødé 工具\\unins000.exe"'),
            ("QuietUninstallString", '"C:\\Program Files\\Ünïcødé 工具\\unins000.exe" /SILENT')]),
        reg_key(UNINSTALL + "{11111111-2222-3333-4444-555555555555}", 1793320000, [
            ("DisplayName", "Raye MSI Thing"), ("Publisher", "Raye Test Publisher"),
            ("DisplayVersion", "3.0"), ("WindowsInstaller", 1),
            ("UninstallString", "MsiExec.exe /I{11111111-2222-3333-4444-555555555555}")]),
        reg_key(UNINSTALL + "HiddenPart", 1793320000, [
            ("DisplayName", "Hidden part"), ("SystemComponent", 1)]),
        reg_key(UNINSTALL + "KB123456", 1793320000, [
            ("DisplayName", "Update for Raye Test Tool"), ("ParentKeyName", "RayeTestTool")]),
        reg_key(UNINSTALL + "NoName", 1793320000, [("Publisher", "Nobody")]),
        reg_key(UNINSTALL + "Sticky", 1793320000, [
            ("DisplayName", "Sticky Driver"), ("NoRemove", 1),
            ("UninstallString", "C:\\windows\\sticky.exe")]),
        reg_key("Software\\Microsoft\\Windows\\CurrentVersion\\Run", 1793300000, [
            ("Thing", "C:\\thing.exe")]),
    ]
    text = HEAD + "\n".join(keys)
    # a binary value wrapped over two lines, as Wine does past 80 columns
    text += ('\n[Software\\\\Raye\\\\Binary] 1793300000\n"Blob"=hex:00,01,02,03,04,05,06,07,08,09,0a,0b,0c,0d,0e,0f,\\\n'
             '  10,11,12\n"After"="ok"\n')
    return text.encode("utf-8")


def user_reg():
    head = "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n\n#arch=win64\n\n"
    keys = [
        reg_key(UNINSTALL + "Chatty", 1793330000, [
            ("DisplayName", "Chatty"), ("Publisher", "Chatty Inc."),
            ("DisplayVersion", "0.9.1"),
            ("DisplayIcon", "C:\\users\\raye\\AppData\\Local\\Chatty\\app.ico"),
            ("InstallLocation", "C:\\users\\raye\\AppData\\Local\\Chatty"),
            ("UninstallString", 'C:\\users\\raye\\AppData\\Local\\Chatty\\Update.exe --uninstall'),
            ("Comments", 'Line one\nquote " and back\\slash')]),
    ]
    return (head + "\n".join(keys)).encode("utf-8")


def template_reg():
    return (HEAD + "\n".join(TEMPLATE_KEYS)).encode("utf-8")


# --- the set ------------------------------------------------------------

def files():
    v_en = [("CompanyName", "Raye Test Publisher"), ("FileDescription", "Raye Test Tool Setup"),
            ("FileVersion", "1.2.3.4"), ("ProductName", "Raye Test Tool"),
            ("ProductVersion", "1.2.3"), ("OriginalFilename", "tool.exe")]
    v_de = [("CompanyName", "Raye Testfirma"), ("ProductName", "Raye Testwerkzeug")]
    tool64 = icon_resources({
        1: [(256, png(256, VIOLET, MAGENTA), 32), (48, dib(48, GREEN), 32), (32, dib(32, BLUE), 32)],
        200: [(32, dib(32, RED), 32)],
    })
    tool64[16] = {1: version_resource([("040704B0", v_de), ("040904B0", v_en)])}
    tool32 = icon_resources({101: [(48, png(48, MAGENTA), 32)]})
    tool32[16] = {1: version_resource([("041104B0", [("ProductName", "Ünïcødé 工具"),
                                                      ("CompanyName", "Ça Marche Ltd"),
                                                      ("FileVersion", "1.0")])], (1, 0, 0, 0))}
    noicon = {16: {1: version_resource([("040904E4", [("FileDescription", "Uninstaller")])], (2, 0, 0, 0))}}
    return {
        "tool64.exe": pe(tool64, True, signed_data("Raye Test Publisher, Inc.")),
        "tool32.exe": pe(tool32, False),
        "noicon.exe": pe(noicon, True),
        "bare.exe": pe({}, True),
        "truncated.exe": pe(tool64, True)[:0x1A0],
        "app.ico": ico_file([(64, png(64, GREEN), 32), (16, dib(16, RED), 32)]),
        "unicode.lnk": lnk(target="C:\\Program Files\\Ünïcødé 工具\\tool.exe",
                           unicode_path=True, name="Ünïcødé tool",
                           args='--start "a b"', workdir="C:\\Program Files\\Ünïcødé 工具",
                           icon="C:\\Program Files\\Raye Test\\tool.exe", icon_index=1),
        "idlist.lnk": lnk(idlist="C:\\Program Files\\Raye Test\\tool.exe"),
        "env.lnk": lnk(env="%ProgramFiles%\\Raye Test\\tool.exe", icon="%ProgramFiles%\\Raye Test\\tool.exe",
                       icon_index=-200),
        "ansi.lnk": lnk(target="C:\\Program Files\\Café\\tool.exe", unicode=False,
                        name="Café tool", relative="..\\..\\Café\\tool.exe"),
        "missing-icon.lnk": lnk(target="C:\\Program Files\\Raye Test\\tool.exe",
                                icon="C:\\nowhere\\missing.ico"),
        "uninstall.lnk": lnk(target="C:\\Program Files\\Raye Test\\unins000.exe"),
        "readme.lnk": lnk(target="C:\\Program Files\\Raye Test\\readme.txt"),
        "chatty.lnk": lnk(target="C:\\users\\raye\\AppData\\Local\\Chatty\\Chatty.exe",
                          icon="C:\\users\\raye\\AppData\\Local\\Chatty\\app.ico"),
        "system.reg": system_reg(),
        "user.reg": user_reg(),
        "template-system.reg": template_reg(),
    }


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else OUT
    os.makedirs(out, exist_ok=True)
    for name, data in sorted(files().items()):
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)
    print(f"{len(files())} files in {out}")


if __name__ == "__main__":
    main()
