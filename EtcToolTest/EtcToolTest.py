
import argparse
import filecmp
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description="Validate EtcTool output.")
    parser.add_argument("executable")
    parser.add_argument("image")
    parser.add_argument("expected")

    arguments = parser.parse_args()

    with tempfile.NamedTemporaryFile() as file:
        subprocess.check_call([
            arguments.executable,
            arguments.image,
            "-output", file.name,
        ])
        file.flush()
        assert filecmp.cmp(file.name, arguments.expected)


if __name__ == "__main__":
    main()
