#!/usr/bin/python2
from __future__ import (
    print_function,
    division
)

import sys
import math


class RHDSData(object):
    def __init__(
            self,
            stream=sys.stdout,
            users=10000,
            groups=100,
            grps_puser=20,
            nest_level=10,
            ngrps_puser=10,
            domain="redhat.com",
            basedn="dc=example,dc=com",
    ):
        self.users = users
        self.groups = groups
        self.basedn = basedn
        self.domain = domain
        self.stream = stream

        self.grps_puser = grps_puser
        self.nest_level = nest_level
        self.ngrps_puser = ngrps_puser

        self.user_defaults = {
            'objectClass': [
                'person',
                'top',
                'inetorgperson',
                'organizationalperson',
                'inetuser',
                'posixaccount'],
            'uidNumber': ['-1'],
            'gidNumber': ['-1'],
        }

        self.group_defaults = {
            'objectClass': [
                'top',
                'inetuser',
                'posixgroup',
                'groupofnames'],
            'gidNumber': [-1],
        }

    def put_entry(self, entry):
        """
        Abstract method, implementation depends on if we want just print LDIF,
        or update LDAP directly
        """
        raise NotImplementedError()

    def gen_user(self, uid):
        user = dict(self.user_defaults)
        user['dn'] = 'uid={uid},ou=people,{suffix}'.format(
            uid=uid,
            suffix=self.basedn,
        )
        user['uid'] = [uid]
        user['displayName'] = ['{} {}'.format(uid, uid)]
        user['sn'] = [uid]
        user['homeDirectory'] = ['/other-home/{}'.format(uid)]
        user['mail'] = ['{uid}@{domain}'.format(
            uid=uid, domain=self.domain)]
        user['givenName'] = [uid]
        user['cn'] = ['{} {}'.format(uid, uid)]

        return user

    def username_generator(self, start, stop, step=1):
        for i in range(start, stop, step):
            yield 'user%s' % i

    def gen_group(self, name, members=(), group_members=()):
        group = dict(self.group_defaults)
        group['dn'] = 'cn={name},ou=groups,{suffix}'.format(
            name=name,
            suffix=self.basedn,
        )
        group['cn'] = [name]
        group['member'] = ['uid={uid},ou=people,{suffix}'.format(
            uid=uid,
            suffix=self.basedn,
        ) for uid in members]
        group['member'].extend(
            ['cn={name},ou=groups,{suffix}'.format(
                name=name,
                suffix=self.basedn,
            ) for name in group_members])
        return group

    def groupname_generator(self, start, stop, step=1):
        for i in range(start, stop, step):
            yield 'group%s' % i

    def gen_users_and_groups(self):
        self.__gen_entries_with_groups(
            self.users,
            self.groups,
            self.grps_puser,
            self.ngrps_puser,
            self.nest_level,
            self.username_generator, self.gen_user,
            self.groupname_generator, self.gen_group
        )

    def __gen_entries_with_groups(
            self,
            num_of_entries,
            num_of_groups,
            groups_per_entry,
            nested_groups_per_entry,
            max_nesting_level,
            gen_entry_name_f, gen_entry_f,
            gen_group_name_f, gen_group_f
    ):
        assert num_of_groups % groups_per_entry == 0
        assert num_of_groups >= groups_per_entry
        assert groups_per_entry > nested_groups_per_entry
        assert max_nesting_level > 0
        assert nested_groups_per_entry > 0
        assert (
            groups_per_entry - nested_groups_per_entry >
            int(math.ceil(nested_groups_per_entry / float(max_nesting_level)))
        ), (
            "At least {} groups is required to generate proper amount of "
            "nested groups".format(
                nested_groups_per_entry +
                int(math.ceil(
                    nested_groups_per_entry / float(max_nesting_level))
                )
            )
        )

        for uid in gen_entry_name_f(0, num_of_entries):
            self.put_entry(gen_entry_f(uid))

        # create N groups per entry, <num_of_nested_groups> of them are nested
        #   User/Host (max nesting level = 2)
        #   |
        #   +--- G1 --- G2 (nested) --- G3 (nested, max level)
        #   |
        #   +--- G5 --- G6 (nested)
        #   |
        #   ......
        #   |
        #   +--- GN

        # how many members should be added to groups (set of groups_per_entry
        # have the same members)
        entries_per_group = num_of_entries // (num_of_groups // groups_per_entry)

        # generate groups and put users there
        for i in range(num_of_groups // groups_per_entry):

            uids = list(gen_entry_name_f(
                i * entries_per_group,
                (i + 1) * entries_per_group
            ))

            # per user
            last_grp_name = None
            nest_lvl = 0
            nested_groups_added = 0

            for group_name in gen_group_name_f(
                            i * groups_per_entry,
                            (i + 1) * groups_per_entry,
            ):
                # create nested groups first
                if nested_groups_added < nested_groups_per_entry:
                    if nest_lvl == 0:
                        # the top group
                        self.put_entry(
                            gen_group_f(
                                group_name,
                                members=uids
                            )
                        )
                        nest_lvl += 1
                        nested_groups_added += 1
                    elif nest_lvl == max_nesting_level:
                        # the last level group this group is not nested
                        self.put_entry(
                            gen_group_f(
                                group_name,
                                group_members=[last_grp_name],
                            )
                        )
                        nest_lvl = 0
                    else:
                        # mid level group
                        self.put_entry(
                            gen_group_f(
                                group_name,
                                group_members=[last_grp_name]
                            )
                        )
                        nested_groups_added += 1
                        nest_lvl += 1

                    last_grp_name = group_name
                else:
                    # rest of groups have direct membership
                    if nest_lvl != 0:
                        # assign the last nested group if exists
                        self.put_entry(
                            gen_group_f(
                                group_name,
                                members=uids,
                                group_members=[last_grp_name],
                            )
                        )
                        nest_lvl = 0
                    else:
                        self.put_entry(
                            gen_group_f(
                                group_name,
                                members=uids
                            )
                        )

    def __generate_entries_with_users_groups(
            self,
            num_of_entries_direct_members,
            num_of_entries_indirect_members,
            entries_per_user,
            entries_per_group,
            gen_entry_name_f, gen_entry_f,
    ):
        assert num_of_entries_direct_members % entries_per_user == 0
        assert num_of_entries_indirect_members % entries_per_group == 0

        num_of_entries = num_of_entries_direct_members + num_of_entries_indirect_members

        # direct members
        users_per_entry = self.users // (num_of_entries_direct_members // entries_per_user)

        start_user = 0
        stop_user = users_per_entry
        for name in gen_entry_name_f(0, num_of_entries_direct_members):
            self.put_entry(
                gen_entry_f(
                    name,
                    user_members=self.username_generator(start_user, stop_user),
                )
            )
            start_user = stop_user % self.users
            stop_user = start_user + users_per_entry
            stop_user = stop_user if stop_user < self.users else self.users

        groups_per_entry = self.groups // (num_of_entries_indirect_members // entries_per_group)

        # indirect members
        start_group = 0
        stop_group = groups_per_entry
        for name in gen_entry_name_f(num_of_entries_direct_members, num_of_entries):
            self.put_entry(
                gen_entry_f(
                    name,
                    usergroup_members=self.groupname_generator(start_group, stop_group),
                )
            )
            start_group = stop_group % self.groups
            stop_group = start_group + groups_per_entry
            stop_group = stop_group if stop_group < self.groups else self.groups

    def do_magic(self):
        self.gen_users_and_groups()


class RHDSDataLDIF(RHDSData):
    def put_entry(self, entry):
        print(file=self.stream)
        print("dn:", entry['dn'], file=self.stream)
        for k, values in entry.items():
            if k == 'dn':
                continue
            for v in values:
                print("{}: {}".format(k, v), file=self.stream)
        print(file=self.stream)
