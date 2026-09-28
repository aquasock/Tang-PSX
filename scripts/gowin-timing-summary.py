#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Summarize Fmax and setup/hold TNS from Gowin place-and-route reports."""

import argparse
import html
import re
import sys
from pathlib import Path


def tables(report):
    text = report.read_text(errors="ignore")
    for match in re.finditer(r"<table.*?</table>", text, re.S):
        rows = []
        for row in re.findall(r"<tr.*?</tr>", match.group(0), re.S):
            cells = re.findall(r"<t[hd][^>]*>(.*?)</t[hd]>", row, re.S)
            rows.append([html.unescape(re.sub(r"<[^>]+>", "", c)).strip() for c in cells])
        yield rows


def summarize(report):
    fmax, tns = {}, {}
    for rows in tables(report):
        header = rows[0] if rows else []
        if "Actual Fmax" in header:
            for row in rows[1:]:
                fmax[row[1]] = (row[2], row[3])
        elif "Endpoints TNS" in header:
            for row in rows[1:]:
                tns[(row[0], row[1])] = (float(row[2]), int(row[3]))
    return fmax, tns


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("builds", nargs="+", type=Path,
        help="Gowin project directories containing impl/pnr")
    args = parser.parse_args()

    failed = False
    for build in args.builds:
        report = build / "impl/pnr" / f"{build.name}_tr_content.html"
        if not report.exists():
            candidates = sorted((build / "impl/pnr").glob("*_tr_content.html"))
            report = candidates[0] if candidates else report
        if not report.exists():
            print(f"{build}: no timing report")
            failed = True
            continue
        fmax, tns = summarize(report)
        violations = {k: v for k, v in tns.items() if v[0] < 0}
        verdict = "MET" if not violations else "FAILED"
        failed |= bool(violations)
        label = build.parent.name if build.name == "gateware" else build.name
        print(f"{label}: timing {verdict}")
        for clock, (constraint, actual) in fmax.items():
            print(f"  {clock:14s} constraint {constraint:>14s}  Fmax {actual}")
        for (clock, kind), (value, endpoints) in violations.items():
            print(f"  {clock:14s} {kind:5s} TNS {value:.3f} ns over {endpoints} endpoints")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
