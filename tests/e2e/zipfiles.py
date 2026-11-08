"""The output files of tests/e2e/golden.sh, read from the zip archives buf writes (`out: <group>.zip`).

    zipfiles.py sums DIR            "<sha256>  <group>/<path>" for every file of DIR/<group>.zip
    zipfiles.py extract DIR OUT     extract DIR/<group>.zip to OUT/<group>/ (for diffs)

Reading the archives instead of a directory keeps paths that differ only in case apart on file
systems that do not (macOS, Windows). Lines are written as UTF-8 bytes, unsorted.
"""
import hashlib
import os
import sys
import zipfile


def archives(d):
    if not os.path.isdir(d):
        return
    for name in sorted(os.listdir(d)):
        if name.endswith(".zip"):
            yield name[: -len(".zip")], zipfile.ZipFile(os.path.join(d, name))


def main(argv):
    if len(argv) == 3 and argv[1] == "sums":
        out = sys.stdout.buffer
        for group, z in archives(argv[2]):
            for info in z.infolist():
                if not info.is_dir():
                    digest = hashlib.sha256(z.read(info)).hexdigest()
                    out.write(("%s  %s/%s\n" % (digest, group, info.filename)).encode())
        return 0
    if len(argv) == 4 and argv[1] == "extract":
        for group, z in archives(argv[2]):
            z.extractall(os.path.join(argv[3], group))
        return 0
    sys.stderr.write("usage: zipfiles.py sums DIR | extract DIR OUT\n")
    return 2


sys.exit(main(sys.argv))
