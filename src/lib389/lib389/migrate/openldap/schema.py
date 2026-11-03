# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au>
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import glob
import os
import re
from io import StringIO

import ldif
from ldap.schema.models import AttributeType, ObjectClass

from lib389.schema import Resolver, Schema
from lib389.utils import ensure_list_str

_SCHEMA_START_RE = re.compile(
    r'^\s*(attributetype|objectclass)\s*(\(.*)?$',
    re.IGNORECASE,
)
_SCHEMA_KIND_RE = re.compile(
    r'^\s*(attributetype|objectclass)\s+(.*)$',
    re.IGNORECASE | re.DOTALL,
)

DEFAULT_SCHEMADIR_FILENAME = '98openldap-migrated.ldif'
SCHEMA_DN_LINE = 'dn: cn=schema'


def _toggle_schema_quote(ch, in_quote, text, index):
    if ch != "'":
        return in_quote
    if in_quote and index > 0 and text[index - 1] == '\\':
        return True
    return not in_quote


def _normalize_schema_whitespace(text):
    """Collapse whitespace runs to a single space outside single-quoted strings."""
    out = []
    in_quote = False
    need_space = False
    for index, ch in enumerate(text):
        if in_quote:
            out.append(ch)
            in_quote = _toggle_schema_quote(ch, in_quote, text, index)
            continue
        if ch == "'":
            if need_space and out and out[-1] not in ('(',):
                out.append(' ')
            need_space = False
            out.append(ch)
            in_quote = True
            continue
        if ch.isspace():
            need_space = True
            continue
        if need_space and out and out[-1] not in ('(',):
            out.append(' ')
        need_space = False
        if out and out[-1] == '(':
            out.append(' ')
        out.append(ch)
    return ''.join(out).strip()


def _paren_balance(text):
    """Return net open-paren count outside single-quoted strings."""
    balance = 0
    in_quote = False
    for index, ch in enumerate(text):
        if in_quote:
            in_quote = _toggle_schema_quote(ch, in_quote, text, index)
            continue
        if ch == "'":
            in_quote = True
        elif ch == '(':
            balance += 1
        elif ch == ')':
            balance -= 1
    return balance


def _normalize_schema_definition(joined):
    """Turn a single-line openldap schema definition into 389 LDIF attribute line."""
    joined = _normalize_schema_whitespace(joined)
    match = _SCHEMA_KIND_RE.match(joined)
    if not match:
        raise ValueError('Invalid schema definition: %r' % (joined[:80],))
    kind = match.group(1).lower()
    body = match.group(2).strip()
    if kind == 'attributetype':
        return 'attributeTypes: %s' % body
    return 'objectClasses: %s' % body


def _comment_non_schema_line(line):
    stripped = line.rstrip('\n')
    if stripped.lstrip().startswith('#'):
        return stripped
    return '# %s' % stripped


def convert_openldap_schema_text(text):
    """Convert OpenLDAP slapd.conf-style schema text to 389 schema LDIF.

    Comment lines (#...) are preserved. Blank lines become ``#``. Multi-line
    attributetype/objectclass definitions are joined onto one line each. A single
    ``dn: cn=schema`` line is inserted before the first converted definition.
    Other lines (e.g. objectidentifier) are preserved as comments.

    Whitespace is collapsed outside single-quoted strings only. Parenthesis
    matching ignores parentheses inside quoted strings.
    """
    output_lines = []
    dn_written = False
    pending = None

    def flush_pending():
        nonlocal pending, dn_written
        if pending is None:
            return
        schema_line = _normalize_schema_definition(pending)
        if not dn_written:
            output_lines.append(SCHEMA_DN_LINE)
            dn_written = True
        output_lines.append(schema_line)
        pending = None

    for raw_line in text.splitlines():
        if pending is not None:
            pending = '%s %s' % (pending, raw_line.strip())
            if _paren_balance(pending) <= 0:
                flush_pending()
            continue

        if not raw_line.strip():
            output_lines.append('#')
            continue

        if raw_line.lstrip().startswith('#'):
            output_lines.append(raw_line.rstrip('\n'))
            continue

        if _SCHEMA_START_RE.match(raw_line):
            pending = raw_line.strip()
            if _paren_balance(pending) <= 0:
                flush_pending()
            continue

        output_lines.append(_comment_non_schema_line(raw_line))

    if pending is not None and _paren_balance(pending) != 0:
        raise ValueError('Unclosed attributetype/objectclass definition at end of input')
    flush_pending()
    return '\n'.join(output_lines) + '\n'


def resolve_schema_paths(paths):
    """Expand paths to a sorted list of ``*.schema`` files."""
    files = []
    for path in paths:
        if os.path.isdir(path):
            files.extend(sorted(glob.glob(os.path.join(path, '*.schema'))))
        elif path.endswith('.schema') and os.path.isfile(path):
            files.append(path)
        else:
            raise ValueError('Not an OpenLDAP .schema file or directory: %s' % path)
    if not files:
        raise ValueError('No .schema files found in %s' % (paths,))
    return files


def convert_openldap_schema_files(paths):
    """Convert one or more ``.schema`` files or directories into one LDIF document."""
    schema_files = resolve_schema_paths(paths)
    chunks = []
    for schema_file in schema_files:
        with open(schema_file, 'r', encoding='utf-8', errors='replace') as handle:
            chunks.append(convert_openldap_schema_text(handle.read()))
    if len(chunks) == 1:
        return chunks[0]

    merged = []
    dn_seen = False
    for chunk in chunks:
        for line in chunk.splitlines():
            if line == SCHEMA_DN_LINE:
                if dn_seen:
                    continue
                dn_seen = True
            merged.append(line)
    return '\n'.join(merged) + '\n'


def write_schema_ldif(output_path, ldif_text):
    """Write ``ldif_text`` to ``output_path``. Never overwrites an existing file.

    Callers must use a new path or remove the file first. Each schemadir file should
    hold one ``cn=schema`` entry; additional definitions from another conversion belong
    in a separate ``*.ldif`` (the server merges all files on reload), not by silently
    merging into one file here.
    """
    if os.path.exists(output_path):
        raise ValueError('Schema LDIF already exists: %s' % output_path)
    with open(output_path, 'w', encoding='utf-8') as handle:
        handle.write(ldif_text)


def _ldif_text_to_schema_values(ldif_text):
    """Parse converted LDIF and return (attributeTypes, objectClasses) value lists."""
    parser = ldif.LDIFRecordList(StringIO(ldif_text))
    parser.parse()
    if not parser.all_records:
        return [], []
    attrs = parser.all_records[0][1]
    attr_types = []
    object_classes = []
    for key, val in attrs.items():
        key_l = key.lower()
        if key_l == 'attributetypes':
            attr_types = ensure_list_str(val)
        elif key_l == 'objectclasses':
            object_classes = ensure_list_str(val)
    return attr_types, object_classes


def normalize_schema_ldif_for_schemadir(ldif_text):
    """Build a schemadir-safe LDIF entry from converted schema text.

    Schema reload reads every ``*.ldif`` in the instance schemadir and expects
    each file to be a single ``cn=schema`` entry without comment lines between
    attributes. The OpenLDAP conversion preserves blank lines as ``#``, which is
    unsuitable for on-disk schema files.
    """
    attr_types, object_classes = _ldif_text_to_schema_values(ldif_text)
    if not attr_types and not object_classes:
        raise ValueError('No attributeTypes or objectClasses found in schema LDIF')
    lines = [SCHEMA_DN_LINE]
    for attr_def in attr_types:
        lines.append('attributetypes: %s' % attr_def)
    for oc_def in object_classes:
        lines.append('objectclasses: %s' % oc_def)
    return '\n'.join(lines) + '\n'


def _schema_name_set(names):
    if not names:
        return set()
    return set(n.lower() for n in names)


def _norm_syntax(syntax):
    if syntax is None or syntax == '':
        return None
    return syntax.lower()


def _norm_mr(mr):
    if mr is None or mr == '':
        return None
    return mr.lower()


# 389 may omit this from objectClass MUST; OpenLDAP-style LDIF often lists it.
_OBJECTCLASS_IGNORED_IN_MUST = frozenset({'objectclass'})


def _resolved_attr_set(attr_names, resolver):
    """Lowercased canonical attribute names from MUST/MAY tuples."""
    out = set()
    for name in attr_names:
        r = resolver.resolve(name)
        if r:
            out.add(r.lower())
    return out


def _oc_must_normalized(oc, resolver):
    s = _resolved_attr_set(oc.must, resolver)
    s -= _OBJECTCLASS_IGNORED_IN_MUST
    return s


def _find_attributetype(schema, new_at):
    for ds_at in schema.get_attributetypes():
        if ds_at.oid == new_at.oid:
            return ds_at
    new_names = _schema_name_set(new_at.names)
    for ds_at in schema.get_attributetypes():
        if new_names & _schema_name_set(ds_at.names):
            return ds_at
    return None


def _find_objectclass(schema, new_oc):
    for ds_oc in schema.get_objectclasses():
        if ds_oc.oid == new_oc.oid:
            return ds_oc
    new_names = _schema_name_set(new_oc.names)
    for ds_oc in schema.get_objectclasses():
        if new_names & _schema_name_set(ds_oc.names):
            return ds_oc
    return None


def _objectclass_inconsistent(new_oc, ds_oc, resolver):
    """True when definitions disagree on identity or incompatible semantics.

    MUST is compared after dropping ``objectClass`` (389 often omits it). MAY must
    be a subset of the server's: the directory may list inherited or unioned MAY
    attributes not present in the source string.
    """
    if new_oc.oid != ds_oc.oid:
        return True
    if _schema_name_set(new_oc.names) != _schema_name_set(ds_oc.names):
        return True
    if int(new_oc.kind) != int(ds_oc.kind):
        return True
    if _schema_name_set(new_oc.sup) != _schema_name_set(ds_oc.sup):
        return True
    new_must = _oc_must_normalized(new_oc, resolver)
    ds_must = _oc_must_normalized(ds_oc, resolver)
    if new_must != ds_must:
        return True
    new_may = _resolved_attr_set(new_oc.may, resolver)
    ds_may = _resolved_attr_set(ds_oc.may, resolver)
    if not new_may.issubset(ds_may):
        return True
    return False


def _attributetype_inconsistent(new_at, ds_at):
    if new_at.oid != ds_at.oid:
        return True
    if bool(new_at.single_value) != bool(ds_at.single_value):
        return True
    if bool(new_at.obsolete) != bool(ds_at.obsolete):
        return True
    if bool(new_at.collective) != bool(ds_at.collective):
        return True
    if bool(new_at.no_user_mod) != bool(ds_at.no_user_mod):
        return True
    if new_at.usage != ds_at.usage:
        return True
    if _norm_syntax(new_at.syntax) != _norm_syntax(ds_at.syntax):
        return True
    if new_at.syntax_len != ds_at.syntax_len:
        return True
    if _norm_mr(new_at.equality) != _norm_mr(ds_at.equality):
        return True
    if _norm_mr(new_at.ordering) != _norm_mr(ds_at.ordering):
        return True
    if _norm_mr(new_at.substr) != _norm_mr(ds_at.substr):
        return True
    if _schema_name_set(new_at.sup) != _schema_name_set(ds_at.sup):
        return True
    if _schema_name_set(new_at.names) != _schema_name_set(ds_at.names):
        return True
    return False


def _add_attributetype_if_needed(schema, attr_def, log):
    new_at = AttributeType(attr_def)
    ds_at = _find_attributetype(schema, new_at)
    if ds_at is None:
        log.info('Adding attributeTypes: %s', attr_def[:80])
        schema.add('attributeTypes', attr_def)
        return
    if _attributetype_inconsistent(new_at, ds_at):
        name = new_at.names[0] if new_at.names else new_at.oid
        raise ValueError(
            'attributeTypes %s conflicts with existing definition (oid %s)' % (
                name, ds_at.oid))
    log.info(
        'Skipping attributeTypes %s (already present with matching definition)',
        new_at.names[0] if new_at.names else new_at.oid)


def _add_objectclass_if_needed(schema, oc_def, log):
    new_oc = ObjectClass(oc_def)
    ds_oc = _find_objectclass(schema, new_oc)
    if ds_oc is None:
        log.info('Adding objectClasses: %s', oc_def[:80])
        schema.add('objectClasses', oc_def)
        return
    resolver = Resolver(schema.get_attributetypes())
    if _objectclass_inconsistent(new_oc, ds_oc, resolver):
        name = new_oc.names[0] if new_oc.names else new_oc.oid
        raise ValueError(
            'objectClasses %s conflicts with existing definition (oid %s)' % (
                name, ds_oc.oid))
    log.info(
        'Skipping objectClasses %s (already present with matching definition)',
        new_oc.names[0] if new_oc.names else new_oc.oid)


def apply_schema_ldif_online(inst, ldif_text, log):
    """Add schema elements to the running server over LDAP.

    Skips definitions that are already present when they match the requested
    definition (object classes: kind, SUP, normalized MUST, and requested MAY
    covered by the server; attribute types: syntax and matching rules). Raises if
    an existing definition conflicts. Safe to retry after a successful run.
    """
    schema = Schema(inst)
    attr_types, object_classes = _ldif_text_to_schema_values(ldif_text)
    if not attr_types and not object_classes:
        raise ValueError('No attributeTypes or objectClasses found in schema LDIF')
    for attr_def in attr_types:
        _add_attributetype_if_needed(schema, attr_def, log)
    for oc_def in object_classes:
        _add_objectclass_if_needed(schema, oc_def, log)


def _remove_schemadir_ldif_on_reload_failure(path, log):
    """Remove a schemadir file if schema reload failed so startup is not broken."""
    try:
        if os.path.isfile(path):
            os.remove(path)
            if log:
                log.warning(
                    'Removed invalid schema LDIF after reload failure: %s', path)
    except OSError as err:
        if log:
            log.warning('Could not remove schema LDIF %s: %s', path, err)


def install_schema_ldif_to_schemadir(inst, ldif_text, filename=None, log=None):
    """Copy LDIF into the instance schema directory and reload schema from disk.

    Writes only the definitions from this conversion (``write_schema_ldif`` refuses
    to overwrite). To add more schema later, use another ``*.ldif`` name or delete
    the target first; 389 merges schema across all schemadir files on reload.

    Waits for the schema reload task. If task creation fails, the reload exits
    non-zero, or the task does not complete in time, the new LDIF is removed so a
    later server start does not load invalid schema.
    """
    if filename is None or filename == '':
        filename = DEFAULT_SCHEMADIR_FILENAME
    if not filename.endswith('.ldif'):
        filename = '%s.ldif' % filename

    target = os.path.join(inst.schemadir, filename)
    schemadir_ldif = normalize_schema_ldif_for_schemadir(ldif_text)
    if log:
        log.info('Writing schema LDIF to %s', target)
    write_schema_ldif(target, schemadir_ldif)
    os.chmod(target, 0o644)

    schema = Schema(inst)
    try:
        task = schema.reload(schema_dir=inst.schemadir)
    except Exception:
        _remove_schemadir_ldif_on_reload_failure(target, log)
        raise

    if log:
        log.info('Waiting for schema reload task ...')
    task.wait()
    exit_code = task.get_exit_code()
    if exit_code is None or exit_code != 0:
        _remove_schemadir_ldif_on_reload_failure(target, log)
        if exit_code is None:
            raise ValueError(
                'Schema reload did not complete; removed %s to avoid breaking '
                'server startup. Fix the schema and retry.' % target)
        raise ValueError(
            'Schema reload failed with exit code %s; removed %s to avoid breaking '
            'server startup' % (exit_code, target))

    if log:
        log.info('Schema reload completed successfully')
    return task
