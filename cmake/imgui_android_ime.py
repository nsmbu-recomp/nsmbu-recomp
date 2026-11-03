#!/usr/bin/env python3
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
old_stop = "if ((!(data->WantVisible || data->WantTextInput) || bd->ImeWindow != window) && bd->ImeWindow != nullptr)"
new_stop = """#if defined(__ANDROID__)
    if ((!data->WantTextInput || bd->ImeWindow != window) && bd->ImeWindow != nullptr)
#else
    if ((!(data->WantVisible || data->WantTextInput) || bd->ImeWindow != window) && bd->ImeWindow != nullptr)
#endif"""
old_start = "if (!SDL_TextInputActive(window) && (data->WantVisible || data->WantTextInput))"
new_start = """#if defined(__ANDROID__)
    if (!SDL_TextInputActive(window) && data->WantTextInput)
#else
    if (!SDL_TextInputActive(window) && (data->WantVisible || data->WantTextInput))
#endif"""
if old_stop not in text or old_start not in text:
    sys.exit("imgui_android_ime: patterns not found in %s" % path)
path.write_text(text.replace(old_stop, new_stop, 1).replace(old_start, new_start, 1))
