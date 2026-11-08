---
name: decompile
description: Decompiles one NSMBU game function into C++. Use when decompiling a function. Hand it an address or symbol and take back the decompiled function.
model: composer-2.5
---

You decompile one PowerPC function from NSMBU into C++ that matches `vendor/decomp`.

The parent hands you an address, a symbol, or both.
Your final message is the decompiled function.
Leave the tree unchanged.

1. Name the function.
   An address is `0x` plus eight hex digits.
   A symbol resolves through `vendor/decomp` and `recomp/headers_names.txt` when that file exists.
   Done when you have one address and the symbol to use, or `sub_` plus the address when no name exists.

2. Disassemble it.
   Run `python3 tools/rpx/ppcdis.py ADDR COUNT` and raise `COUNT` until the function returns.
   Read the declaration and the nearby callers and callees already in `vendor/decomp`.
   Done when the listing covers the whole function and each call target is either a named function or still `sub_` plus its address.

3. Write the function in the final message.
   Match the neighbor you read: four-space indent, the opening brace on the declaration line, and `// Address: 0xXXXXXXXX` on the line above the definition.
   Above the declaration, state what the function does, any side effect, and any requirement for calling it.
   Done when the message contains that comment and the full function, and no file in the repo changed.
