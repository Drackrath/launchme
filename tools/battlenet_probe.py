"""Read-only Windows schema probe. Outputs aggregates only, never account values.

Development tool (Python stdlib); the launcher has no Python dependency.
--since is the running Agent's UTC creation time as Unix seconds. No cookies,
login-cache rows, ledger values, paths, or raw log lines are emitted/saved.
"""
import argparse
from collections import Counter
from datetime import datetime
import json
import os
from pathlib import Path
import re
import sqlite3
import time


def report(section, **counts):
    print(section + " " + " ".join(f"{k}={int(v)}" for k, v in counts.items()))


def read(path, limit=8 * 1024 * 1024):
    if path.stat().st_size > limit:
        raise ValueError("limit")
    return path.read_bytes()


def proto(raw):
    offset = 0

    def varint():
        nonlocal offset
        value = 0
        for shift in range(0, 64, 7):
            byte = raw[offset]
            offset += 1
            if shift == 63 and byte > 1:
                raise ValueError("overflow")
            value |= (byte & 127) << shift
            if not byte & 128:
                return value
        raise ValueError("varint")

    out = []
    while offset < len(raw):
        tag = varint()
        field, wire = tag >> 3, tag & 7
        if not field or len(out) >= 10000:
            raise ValueError("fields")
        if wire == 0:
            value = varint()
        else:
            length = varint() if wire == 2 else {1: 8, 5: 4}[wire]
            value = raw[offset:offset + length]
            offset += length
            if len(value) != length:
                raise ValueError("truncated")
        out.append((field, wire, value))
    return out


def field(fields, key, wire=2, default: bytes | int | None = b""):
    return next((v for k, w, v in fields if k == key and w == wire), default)


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--since", type=float, default=time.time())
    since = args.parse_args().since
    local = Path(os.environ["LOCALAPPDATA"]) / "Battle.net"
    agent = Path(os.environ["PROGRAMDATA"]) / "Battle.net" / "Agent"
    db = local / "CachedData.db"
    report("files", cached_db=db.is_file(), cache=(local / "Cache").is_dir(),
           product_db=(agent / "product.db").is_file(), aggregate=(agent / "aggregate.json").is_file())
    try:
        con = sqlite3.connect(db.as_uri() + "?mode=ro", uri=True, timeout=1)
        con.execute("PRAGMA query_only=ON")
        con.execute("BEGIN")
        tables = {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        rows = con.execute("SELECT value FROM key_value_store WHERE key=?", ("features_cached_data_points",)).fetchall()
        data = json.loads(rows[0][0]) if len(rows) == 1 else {}
        licenses = data.get("licenses")
        report("account_schema", table="key_value_store" in tables, feature_rows=len(rows),
               licenses_array=isinstance(licenses, list), licenses=len(licenses) if isinstance(licenses, list) else 0,
               numeric_licenses=sum(type(v) is int for v in licenses) if isinstance(licenses, list) else 0,
               account_id=type(data.get("account_id")) is int,
               region=isinstance(data.get("account_region"), str),
               game_accounts=isinstance(data.get("game_accounts"), (dict, list)),
               flags=isinstance(data.get("flags"), (dict, list)),
               explicit_freshness=any(k in data for k in ("timestamp", "updated_at", "expires_at")),
               db_written_this_session=db.stat().st_mtime >= since)
        # Schema presence alone cannot prove the actively signed-in identity.
        columns = {r[1] for r in con.execute("PRAGMA table_info(login_cache)")} if "login_cache" in tables else set()
        report("identity_schema", login_cache="login_cache" in tables,
               account_id_column="account_id" in columns, active_column="active" in columns,
               account_binding_verified=False)
        con.close()
    except (OSError, sqlite3.Error, ValueError, TypeError):
        report("account_schema", readable=False)
    products = []
    try:
        records = proto(read(agent / "product.db"))
        flags = Counter()
        infrastructure = Counter()
        for k, w, raw in records:
            if (k, w) != (1, 2):
                continue
            p = proto(raw)
            settings = proto(field(p, 3))
            base = proto(field(proto(field(p, 4)), 1))
            code = field(p, 2)
            state = tuple(field(base, n, 0, None) for n in (1, 2, 3))
            if code in (b"agent", b"bna"):
                infrastructure["records"] += 1
                uid = field(p, 1)
                infrastructure["uid_dot"] += isinstance(uid, bytes) and b"." in uid
                infrastructure["settings_known_wire_mismatch"] += any(k in (1, 2, 6) and w != 2 for k, w, _ in settings)
                infrastructure["base_known_wire_mismatch"] += any((k in (1, 2, 3) and w != 0) or (k == 7 and w != 2) for k, w, _ in base)
                infrastructure["three_boolean_flags"] += all(v in (0, 1) for v in state)
                infrastructure["ready"] += state == (1, 1, 1)
                continue
            flags["ready" if state == (1, 1, 1) else "incomplete" if all(v in (0, 1) for v in state) else "unknown"] += 1
            products.append((field(p, 1), code))
            flags["path_present"] += bool(field(settings, 1))
            flags["code_present"] += bool(code)
        report("product_schema", valid=True, games=len(products), **flags)
        report("infrastructure_schema", **infrastructure)
    except (OSError, ValueError, KeyError, IndexError):
        report("product_schema", valid=False)
    try:
        data = json.loads(read(agent / "aggregate.json"))
        entries = data.get("installed", [])
        codes = {c.decode("utf-8") for _, c in products}
        report("aggregate_schema", valid=isinstance(entries, list), entries=len(entries),
               joined=sum(e.get("product_id") in codes for e in entries),
               timestamps=sum(type(e.get("last_played_timestamp")) is int for e in entries))
    except (OSError, ValueError, TypeError):
        report("aggregate_schema", valid=False)
    counts = Counter()
    defaults = set()
    for index, path in enumerate((local / "Cache").rglob("*")):
        if index >= 20000:
            break
        if not path.is_file():
            continue
        try:
            raw = read(path, 4 * 1024 * 1024)
            if b'"fragment_id"' not in raw:
                continue
            fragment = json.loads(raw)
            counts["fragments"] += 1
            counts["with_rules"] += bool(fragment.get("program_configuration"))
            counts["default_strings"] += "default" in fragment.get("strings", {})
            for product in fragment.get("products", []):
                base = product.get("base", {})
                types = base.get("types", {})
                uid = types.get(base.get("default_product_type", "retail"), {}).get("uid")
                if isinstance(uid, str):
                    defaults.add(uid.encode())
        except (OSError, ValueError, TypeError, AttributeError):
            continue
    report("pub_schema", **counts, local_default_uid_matches=sum(u in defaults for u, _ in products))
    counts = Counter()
    stamp = re.compile(r"\[\w (\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)")
    for path in agent.glob("Agent.*/Logs/*.log"):
        if not path.name.startswith(("Operations-", "AgentUpdate-")):
            continue
        try:
            if path.stat().st_mtime < since:
                continue
            counts["session_files"] += 1
            for line in read(path).decode("utf-8", "replace").splitlines():
                match = stamp.search(line)
                if not match or datetime.strptime(match[1], "%Y-%m-%d %H:%M:%S").timestamp() < since:
                    continue
                counts["session_lines"] += 1
                counts["queued"] += "Queue operation - OP_UPDATE for '" in line
                counts["active"] += "Active operation " in line and "replaced by OP_UPDATE for '" in line
                counts["completed"] += "OP_UPDATE for '" in line and " completed" in line
                counts["progress"] += " Update Progress - " in line
        except (OSError, ValueError):
            continue
    report("agent_logs", **counts)


if __name__ == "__main__":
    main()
