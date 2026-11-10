"""Bakes page/index.html into include/page.h.

page/index.html is the dashboard's source of truth. The TVs normally load it
straight from the repo (see the loader in src/main.cpp); the copy baked into
the firmware by this script is the fallback for when GitHub can't be reached.

Runs automatically before every PlatformIO build (extra_scripts in
platformio.ini). To run it by hand:  py tools/embed_page.py
"""
import os

try:
    Import("env")  # noqa: F821 - provided by PlatformIO
    ROOT = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC = os.path.join(ROOT, "page", "index.html")
DST = os.path.join(ROOT, "include", "page.h")

HEADER = """#pragma once

// GENERATED from page/index.html by tools/embed_page.py - edit that file, not
// this one. This is the built-in copy of the dashboard, used when the TV
// can't fetch the live page from the repo.
static const char PAGE_HTML[] = R"HTML("""
FOOTER = ')HTML";\n'


def embed():
    with open(SRC, encoding="utf-8") as f:
        html = f.read()
    if ')HTML"' in html:
        raise ValueError('page/index.html contains )HTML" which would end the C++ string early')
    out = HEADER + html + FOOTER
    old = None
    if os.path.exists(DST):
        with open(DST, encoding="utf-8") as f:
            old = f.read()
    if out != old:
        with open(DST, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
        print("embed_page: include/page.h updated from page/index.html")


try:
    embed()
except Exception as e:  # never break the build; the existing page.h still works
    print("embed_page: WARNING - page.h not regenerated: %s" % e)
