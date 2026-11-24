# Findings

## Report

The reporter on Windows v0.50 could not paste or load a Wii U common key from a text file for a `.wux` disc image.
They used a `.wua` Cemu archive instead, which needs no keys.

## Status

GitHub marks issue #11 closed.
There are no issue comments in the cached metadata.
The issue closed on 2026-10-24, the same day as v0.6.0 shipped.

## What the code shows

Commit `92eb7704` closes the issue with message "Installer: take typed or pasted 32-hex keys".
The fix lives in the installer setup path for `.wux` disc images, not in `.wua` handling.

Likely symbols:

- `installer/setup.py`: `parse_key`, `decode_key_text`, `read_key_file`, `keys_in_text`, `find_common_key`
- `installer/gui/setup_gui.cpp`: Keys screen paste and `common_key_hex` / `common_key_file` fields
- `installer/test_setup.py`: `test_issue_11_windows_common_key_paste`

The regression test covers Windows-style pasted keys and Notepad UTF-16 key files.
`docs/developer/automation.md` lists that test as closed-issue coverage for #11.

## CHANGELOG

v0.6.0 (2026-10-24) documents the fix under Fixed:

- The Keys screen accepts typed or pasted 32-hex disc and common keys.
- Notepad-saved key files and Cemu `keys.txt` work too.

No later changelog entry mentions #11.