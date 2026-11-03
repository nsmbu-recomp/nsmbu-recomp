# Disc image keys

Skip this if you chose a Cemu archive or an extracted folder.

A disc image needs the disc key and the Wii U common key.
The disc key belongs to that image.
The common key is the same on every Wii U.

On the Keys screen, type or paste each key's 32 hex digits into its own field.
The text field hides the key.
Keys stay in memory until extraction starts.
The setup log never stores keys.

Put a matching 1.3.0 update in an `update/` folder next to the disc image (`code/`, `content/`, `meta/`).
You can also choose that folder in the installer.

Key files are optional.
If a `.key` file with the same name as the image sits next to the image, the installer uses it.
If `common.key` sits next to the image, the installer uses that too.
You can also choose a key file.
A key file can be 16 raw bytes, one line of 32 hex digits, or a Cemu `keys.txt`.
