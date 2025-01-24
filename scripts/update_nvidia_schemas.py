#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import sys
import generate_schema_enums

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))

SHA1 = '08c5bd8240634f652c788d5b8fb07d1f5618ca76'

def main():
    with tempfile.TemporaryDirectory() as repo_dir:
        repo_dir_str = str(repo_dir)
        csdl_dir = os.path.join(SCRIPT_DIR, '..', 'redfish-core', 'schema', 'oem', 'nvidia', 'csdl')
        try:
            shutil.rmtree(csdl_dir)
        except FileNotFoundError:
            pass

        os.makedirs(csdl_dir)

        subprocess.check_call(
            [
                "git",
                "clone",
                "ssh://git@gitlab-master.nvidia.com:12051/dgx/redfish.git",
                repo_dir_str,
            ]
        )
        subprocess.check_call(
            [
                "git",
                "-C", repo_dir_str,
                "reset",
                "--hard",
                SHA1
            ]
        )

        repo_csdl_dir = repo_dir + "/metadata/nvidia-baseboard-csdl/"
        for filename in os.listdir(repo_csdl_dir):
            src = os.path.join(repo_csdl_dir, filename)
            dest = os.path.join(csdl_dir, filename)
            with open(src, "r") as read_file:
                content = read_file.read()
            content = content.replace("\r\n", "\n")

            content = content.replace('Uri="/schemas/v1', 'Uri="/redfish/v1/schema')
            with open(dest, "w") as write_file:
                write_file.write(content)
        
        generate_schema_enums.main()


if __name__ == "__main__":
    main()
