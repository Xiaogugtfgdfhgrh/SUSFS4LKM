#!/usr/bin/env python3
import sys
from pathlib import Path

root = Path(sys.argv[1])
path = root / "sources" / "lkm" / "kernel" / "susfs_opath.c"
text = path.read_text(encoding="utf-8")

replacements = [
    (
        "static int (*opath_close_fd_get_file)(unsigned int fd, struct file **res);",
        "static struct file *(*opath_close_fd_get_file)(unsigned int fd);",
    ),
    (
        "\tif (opath_close_fd_get_file((unsigned int)ret, &victim)) {\n"
        "\t\tatomic_inc(&n_opath_err);",
        "\tvictim = opath_close_fd_get_file((unsigned int)ret);\n"
        "\tif (!victim) {\n"
        "\t\tatomic_inc(&n_opath_err);",
    ),
    (
        "\tif (!opath_close_fd_get_file) {\n"
        "\t\topath_close_fd_get_file = (void *)find_kernel_symbol_exact(\"file_close_fd\");\n"
        "\t\topath_close_fd_name = \"file_close_fd\";\n"
        "\t}\n",
        "",
    ),
    (
        "\t\tpr_warn(\"opath: fget_raw=%px close_fd_get_file=%px file_close_fd=%px - a name was not resolved, no table entry is installed and O_PATH stays visible\\n\",\n"
        "\t\t\t(void *)opath_fget_raw,\n"
        "\t\t\t(void *)find_kernel_symbol_exact(\"close_fd_get_file\"),\n"
        "\t\t\t(void *)find_kernel_symbol_exact(\"file_close_fd\"));",
        "\t\tpr_warn(\"opath: fget_raw=%px close_fd_get_file=%px - a name was not resolved, no table entry is installed and O_PATH stays visible\\n\",\n"
        "\t\t\t(void *)opath_fget_raw,\n"
        "\t\t\t(void *)opath_close_fd_get_file);",
    ),
]

for old, new in replacements:
    if old not in text:
        raise SystemExit(f"missing patch anchor in {path}")
    text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8", newline="\n")
