"""Type-check the app's Java sources against an installed Android platform, without Gradle."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "runtime/android/app/src/main"
FILE_PROVIDER = """package androidx.core.content;
public class FileProvider extends android.content.ContentProvider {
    public static android.net.Uri getUriForFile(android.content.Context c, String a, java.io.File f) { return null; }
    public boolean onCreate() { return true; }
    public android.database.Cursor query(android.net.Uri u, String[] p, String s, String[] a, String o) { return null; }
    public String getType(android.net.Uri u) { return null; }
    public android.net.Uri insert(android.net.Uri u, android.content.ContentValues v) { return null; }
    public int delete(android.net.Uri u, String s, String[] a) { return 0; }
    public int update(android.net.Uri u, android.content.ContentValues v, String s, String[] a) { return 0; }
}
"""


def platform_jar():
    for home in (os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT"), "/usr/lib/android-sdk"):
        if home:
            jars = sorted(Path(home).glob("platforms/android-*/android.jar"),
                          key=lambda path: int(re.sub(r"\D", "", path.parent.name) or 0))
            if jars:
                return jars[-1]
    return None


def resource_class(sources):
    used = {}
    for source in sources:
        for kind, name in re.findall(r"(?<![\w.])R\.(\w+)\.(\w+)", source.read_text()):
            used.setdefault(kind, set()).add(name)
    text = "package org.nsmbrecomp.nsmbu;\npublic final class R {\n"
    for kind, names in sorted(used.items()):
        text += "    public static final class %s {\n" % kind
        text += "".join("        public static final int %s = %d;\n" % (name, index + 1)
                        for index, name in enumerate(sorted(names)))
        text += "    }\n"
    return used, text + "}\n"


class AppSources(unittest.TestCase):
    def test_strings_used_by_the_app_exist(self):
        used, _ = resource_class(sorted((MAIN / "java").rglob("*.java")))
        strings = set(re.findall(r'<string name="(\w+)"', (MAIN / "res/values/strings.xml").read_text()))
        self.assertEqual(sorted(used.get("string", set()) - strings), [])

    def test_java_sources_compile_against_the_platform(self):
        jar = platform_jar()
        if jar is None or not shutil.which("javac"):
            self.skipTest("an Android platform jar and a JDK are required")
        sources = sorted((MAIN / "java").rglob("*.java"))
        with tempfile.TemporaryDirectory() as directory:
            stub = Path(directory) / "stub"
            (stub / "org/nsmbrecomp/nsmbu").mkdir(parents=True)
            (stub / "org/nsmbrecomp/nsmbu/R.java").write_text(resource_class(sources)[1])
            (stub / "androidx/core/content").mkdir(parents=True)
            (stub / "androidx/core/content/FileProvider.java").write_text(FILE_PROVIDER)
            result = subprocess.run(["javac", "--release", "17", "-nowarn", "-Xlint:-options", "-cp", str(jar),
                                     "-d", str(Path(directory) / "classes")] +
                                    [str(path) for path in sources + sorted(stub.rglob("*.java"))],
                                    capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
