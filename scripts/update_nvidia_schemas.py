#!/usr/bin/env python3

import argparse
import os
import shutil
import subprocess
import tempfile

import generate_schema_enums

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))

def updates_are_staged():
    out = subprocess.check_output(
        [
            "git",
            "-C",
            os.path.join(SCRIPT_DIR, ".."),
            "diff",
            "--cached",
        ]
    )
    return out != b""

def main():
    parser = argparse.ArgumentParser(description="Update nvidia schemas to latest")

    parser.add_argument(
        "-i",
        "--ignore-branch",
        help="Ignore whether commit exists on develop",
        action="store_true",
    )
    args = parser.parse_args()

    if updates_are_staged():
        print("Git updates are staged in bmcweb.  Cannot update")
        return

    with tempfile.TemporaryDirectory() as repo_dir:
        repo_dir_str = str(repo_dir)
        csdl_dir = os.path.join(
            SCRIPT_DIR, "..", "redfish-core", "schema", "oem", "nvidia", "csdl"
        )
        enums_dir = os.path.join(
            SCRIPT_DIR, "..", "redfish-core", "include", "generated", "enums"
        )
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
        output = subprocess.check_output(
            [
                "git",
                "-C",
                repo_dir_str,
                "rev-parse",
                "develop",
            ]
        )
        latest = output.decode().strip()
        with open(os.path.join(SCRIPT_DIR, "nvidia_schema_version"), "r") as version:
            sha1 = version.read().strip()

        on_develop = latest == sha1
        if not on_develop:
            print(f"Commit {sha1 } is not latest {latest} is latest on develop.")
            if not args.ignore_branch:
                print(f"Write {latest} into nvidia_schema_version to continue")
                return

        subprocess.check_call(["git", "-C", repo_dir_str, "reset", "--hard", sha1])

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
        subprocess.check_call(
            [
                "git",
                "-C",
                os.path.join(SCRIPT_DIR, ".."),
                "add",
                csdl_dir,
                enums_dir,
                os.path.join(SCRIPT_DIR, "update_nvidia_schemas.py"),
            ]
        )
        if updates_are_staged():
            print("Git updates are staged in bmcweb.  Commiting")
            commit_warning = ""
            if not on_develop:
                commit_warning = "WARNING: Commit was not merged to develop."
            subprocess.check_call(
                [
                    "git",
                    "-C",
                    os.path.join(SCRIPT_DIR, ".."),
                    "commit",
                    "-s",
                    "-m",
                    f"Updating schemas to {sha1}.{commit_warning}",
                ]
            )


if __name__ == "__main__":
    main()
