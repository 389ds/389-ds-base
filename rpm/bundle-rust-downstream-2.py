import subprocess
import json
import re
from typing import Dict, Tuple, Optional, Set

# Constants
IGNORED_RUST_PACKAGES: Set[str] = {"librslapd", "librnsslapd", "slapd", "slapi_r_plugin", "entryuuid", "entryuuid_syntax", "pwdchan"}
IGNORED_NPM_PACKAGES: Set[str] = {"389-console"}
PACKAGE_REGEX = re.compile(r"(.*)@(.*)")

def run_command(command: list[str]) -> Optional[str]:
    """Run a shell command and return its output."""
    try:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as process:
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                raise subprocess.CalledProcessError(process.returncode, command, stderr)
            return stdout
    except subprocess.CalledProcessError as e:
        print(f"Error executing '{' '.join(command)}': {e.stderr}")
        return None

def process_rust_crates(output: str) -> Dict[str, Tuple[str, str]]:
    """Process Rust crates using cargo-license."""
    try:
        crates = json.loads(output)
    except json.JSONDecodeError:
        print("Failed to parse JSON from cargo-license output.")
        return {}

    return {crate['name']: (enclose_if_contains_or(crate['license']), crate['version'])
            for crate in crates if crate['name'] not in IGNORED_RUST_PACKAGES}

def process_npm_packages(output: str) -> Dict[str, Tuple[str, str]]:
    """Process NPM dependencies using license-checker."""
    try:
        packages = json.loads(output)
    except json.JSONDecodeError:
        print("Failed to parse JSON from license-checker output.")
        return {}

    processed_packages = {}
    for package, data in packages.items():
        package_name, package_version = PACKAGE_REGEX.match(package).groups()
        if package_name not in IGNORED_NPM_PACKAGES:
            npm_license = process_npm_license(data['licenses'])
            processed_packages[package_name] = (npm_license, package_version)

    return processed_packages

def process_npm_license(license_data) -> str:
    """Process NPM license data."""
    npm_license = license_data if isinstance(license_data, str) else ' OR '.join(license_data)
    return enclose_if_contains_or(npm_license)

def enclose_if_contains_or(license_str: str) -> str:
    """Enclose the license string in parentheses if it contains 'OR'."""
    return f"({license_str})" if 'OR' in license_str and not license_str.startswith('(') else license_str

def build_provides_lines(rust_crates: Dict[str, Tuple[str, str]], npm_packages: Dict[str, Tuple[str, str]]) -> list[str]:
    """Build the 'Provides:' lines for Rust crates and NPM packages."""
    provides_lines = [f"Provides: bundled(crate({crate}) = {version})" for crate, (_, version) in rust_crates.items()]
    provides_lines += [f"Provides: bundled(npm({package}) = {version})" for package, (_, version) in npm_packages.items()]
    return provides_lines

def create_license_line(rust_crates: Dict[str, Tuple[str, str]], npm_packages: Dict[str, Tuple[str, str]]) -> str:
    """Create the final License line."""
    licenses = {license for _, (license, _) in {**rust_crates, **npm_packages}.items() if license}
    return "License: " + " AND ".join(sorted(licenses))

def main():
    """Main execution function."""
    rust_output = run_command(["cargo", "license", "--json", "--current-dir", "./src"])
    npm_output = run_command(["license-checker", "--json", "--start", "src/cockpit/389-console"])

    if rust_output is None or npm_output is None:
        print("Failed to process dependencies. Ensure cargo-license and license-checker are installed and accessible.")
        return

    rust_crates = process_rust_crates(rust_output)
    npm_packages = process_npm_packages(npm_output)

    provides_lines = build_provides_lines(rust_crates, npm_packages)
    license_line = create_license_line(rust_crates, npm_packages)

    for line in provides_lines:
        print(line)
    print(license_line)

if __name__ == "__main__":
    main()

