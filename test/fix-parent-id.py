#!/usr/bin/env python3
import subprocess
import sys
import os
import getpass
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Dict, Optional, Tuple

PARENTID_OID = "2.16.840.1.113730.3.1.604"
LDAP_CACERT = "/etc/ipa/ca.crt"

STALE_PARENTID = (
    f"( {PARENTID_OID} NAME 'parentid' DESC 'internal server defined attribute type' "
    f"EQUALITY caseIgnoreMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 SINGLE-VALUE "
    f"NO-USER-MODIFICATION USAGE directoryOperation X-ORIGIN 'user defined' )"
)

FIXED_PARENTID = (
    f"( {PARENTID_OID} NAME 'parentid' DESC 'internal server defined attribute type' "
    f"EQUALITY integerMatch SYNTAX 1.3.6.1.4.1.1466.115.121.1.27 SINGLE-VALUE "
    f"NO-USER-MODIFICATION USAGE directoryOperation X-ORIGIN 'user defined' )"
)

server_passwords: Dict[str, str] = {}


def ldap_search(server: str, *args) -> Optional[str]:
    """Execute ldapsearch command"""
    pw = server_passwords.get(server)
    if not pw:
        return None

    env = os.environ.copy()
    env['LDAPTLS_CACERT'] = LDAP_CACERT

    cmd = [
        'timeout', '-k', '5', '10',
        'ldapsearch', '-o', 'ldif-wrap=no', '-o', 'nettimeout=5',
        '-x', '-D', 'cn=Directory Manager', '-w', pw,
        '-H', f'ldaps://{server}'
    ] + list(args)

    try:
        result = subprocess.run(
            cmd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            timeout=15
        )
        return result.stdout if result.returncode == 0 else None
    except (subprocess.TimeoutExpired, subprocess.SubprocessError):
        return None


def get_parentid_equality(server: str) -> str:
    """Get the current parentid EQUALITY setting"""
    output = ldap_search(server, '-b', 'cn=schema', '-s', 'base', 'attributeTypes')
    if not output:
        return "EQUALITY not-found"

    for line in output.splitlines():
        if PARENTID_OID in line:
            # Extract EQUALITY portion
            import re
            match = re.search(r'EQUALITY\s+([a-zA-Z]+)', line)
            if match:
                return f"EQUALITY {match.group(1)}"

    return "EQUALITY not-found"


def test_password(server: str, pw: str) -> int:
    """Test if password works. Returns: 0=OK, 1=wrong password, 2=unreachable"""
    env = os.environ.copy()
    env['LDAPTLS_CACERT'] = LDAP_CACERT

    cmd = [
        'timeout', '-k', '5', '10',
        'ldapsearch', '-o', 'ldif-wrap=no', '-o', 'nettimeout=5',
        '-x', '-D', 'cn=Directory Manager', '-w', pw,
        '-H', f'ldaps://{server}', '-b', '', '-s', 'base'
    ]

    try:
        result = subprocess.run(
            cmd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            timeout=15
        )
        output = result.stdout + result.stderr

        if 'dn:' in output.lower():
            return 0  # OK
        elif 'invalid credentials' in output.lower():
            return 1  # wrong password
        else:
            return 2  # unreachable
    except (subprocess.TimeoutExpired, subprocess.SubprocessError):
        return 2


def fix_one_server(server: str, pw: str) -> Tuple[bool, str, str]:
    """Apply fix to one server. Returns (success, timestamp, error_msg)"""
    env = os.environ.copy()
    env['LDAPTLS_CACERT'] = LDAP_CACERT

    ldif_input = f"""dn: cn=schema
changetype: modify
delete: attributeTypes
attributeTypes: {STALE_PARENTID}
-
add: attributeTypes
attributeTypes: {FIXED_PARENTID}
"""

    cmd = [
        'ldapmodify', '-x', '-D', 'cn=Directory Manager', '-w', pw,
        '-H', f'ldaps://{server}'
    ]

    ts = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]

    try:
        result = subprocess.run(
            cmd,
            env=env,
            input=ldif_input,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            timeout=15
        )

        if result.returncode == 0:
            return (True, ts, "")
        else:
            error = (result.stdout + result.stderr).strip()
            return (False, ts, error)
    except (subprocess.TimeoutExpired, subprocess.SubprocessError) as e:
        return (False, ts, str(e))


def main():
    # Prompt for password
    dm_password = getpass.getpass("Directory Manager password: ")

    # Verify against local server
    rc = test_password('localhost', dm_password)
    if rc == 1:
        print("[!] ERROR: Invalid Directory Manager password.")
        sys.exit(1)
    elif rc == 2:
        print("[!] ERROR: Cannot connect to local DS. Is it running?")
        sys.exit(1)

    # Discover servers
    print()
    print("[*] Discovering IPA servers...")

    # Get LDAP suffix
    try:
        with open('/etc/ipa/default.conf', 'r') as f:
            for line in f:
                if 'basedn' in line:
                    suffix = line.split()[2]
                    break
            else:
                raise ValueError("basedn not found")
    except (FileNotFoundError, ValueError, IndexError):
        print("[!] ERROR: Cannot determine LDAP suffix. Is IPA configured on this host?")
        sys.exit(1)

    server_passwords['localhost'] = dm_password

    output = ldap_search(
        'localhost',
        '-b', f'cn=masters,cn=ipa,cn=etc,{suffix}',
        '-s', 'one',
        '(objectclass=ipaConfigObject)',
        'cn'
    )

    if not output:
        print(f"[!] ERROR: No IPA servers found under cn=masters,cn=ipa,cn=etc,{suffix}")
        sys.exit(1)

    servers = [
        line.split(':')[1].strip()
        for line in output.splitlines()
        if line.startswith('cn:')
    ]

    if not servers:
        print(f"[!] ERROR: No IPA servers found under cn=masters,cn=ipa,cn=etc,{suffix}")
        sys.exit(1)

    print("[+] Found servers:")
    for server in servers:
        print(f"    {server}")

    # Verify DM password on each server
    print()
    print("[*] Verifying Directory Manager credentials on all servers...")

    for server in servers:
        rc = test_password(server, dm_password)
        if rc == 0:
            server_passwords[server] = dm_password
            print(f"  [+] {server}: OK")
        elif rc == 2:
            print(f"  [!] {server}: UNREACHABLE (skipping)")
        else:
            print(f"  [-] {server}: different password")
            while True:
                server_pw = getpass.getpass(f"  [?] Directory Manager password for {server}: ")
                pw_rc = test_password(server, server_pw)
                if pw_rc == 0:
                    server_passwords[server] = server_pw
                    print(f"  [+] {server}: OK")
                    break
                elif pw_rc == 2:
                    print(f"  [!] {server}: UNREACHABLE (skipping)")
                    break
                else:
                    print("  [!] Invalid password, try again.")

    # Check current state
    print()
    print("[*] Checking parentid matching rule on all servers...")
    print()

    affected = []
    for server in servers:
        equality = get_parentid_equality(server)

        if 'caseIgnoreMatch' in equality.lower():
            print(f"  [-] {server}: {equality}  <-- STALE")
            affected.append(server)
        else:
            print(f"  [+] {server}: {equality}")

    if not affected:
        print()
        print("[+] All servers have the correct parentid definition (integerMatch).")
        print("[+] Nothing to fix.")
        sys.exit(0)

    print()

    # Ask for confirmation
    confirm = input("[?] Apply fix to replace caseIgnoreMatch with integerMatch? [y/N] ")
    if confirm.lower() not in ('y', 'yes'):
        print("[*] Aborted.")
        sys.exit(0)

    # Apply fix in parallel
    print()
    print("[*] Applying fix to all affected servers in parallel...")

    results = {}
    with ThreadPoolExecutor(max_workers=len(affected)) as executor:
        future_to_server = {
            executor.submit(fix_one_server, server, server_passwords[server]): server
            for server in affected
            if server in server_passwords
        }

        for future in as_completed(future_to_server):
            server = future_to_server[future]
            results[server] = future.result()

    failed = []
    for server in affected:
        if server not in results:
            print(f"  [!] {server}: SKIPPED (no password)")
            failed.append(server)
            continue

        success, ts, error = results[server]
        if success:
            print(f"  [+] {server}: OK  ({ts})")
        else:
            print(f"  [!] {server}: FAILED  ({ts})")
            error_lines = error.split('\n')[:3]
            for line in error_lines:
                print(f"      {line}")
            failed.append(server)

    if failed:
        print()
        print(f"[!] WARNING: Failed to fix: {' '.join(failed)}")
        print("    Check connectivity and Directory Manager password, then retry.")
        sys.exit(1)

    # Verify fix
    print()
    print("[*] Verifying fix...")
    print()

    all_fixed = True
    for server in servers:
        ts = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
        equality = get_parentid_equality(server)

        if 'caseIgnoreMatch' in equality.lower():
            print(f"  [!] {server}: {equality}  <-- STILL STALE  ({ts})")
            all_fixed = False
        else:
            print(f"  [+] {server}: {equality}  ({ts})")

    print()
    if all_fixed:
        print("[+] All servers now have the correct parentid definition (integerMatch).")
        print("[+] New replica installations should now succeed.")
    else:
        print("[!] Some servers still have the stale definition.")
        print("[!] Schema replication may have reverted the fix. Re-run the script.")
        sys.exit(1)


if __name__ == '__main__':
    main()

