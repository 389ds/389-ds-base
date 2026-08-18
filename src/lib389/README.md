# lib389

`lib389` is a Python library that provides a convenient way to interact with and manage a 389 Directory Server instance. It simplifies common LDAP operations and offers an object-oriented interface for various directory server components.

## Installation

To install `lib389` along with the main 389 Directory Server do:

```bash
pip3 install lib389==3.1.2
```

**Important Note:** When you install `lib389`, ensure that its version matches the version of the 389 Directory Server you plan to connect to. Mismatched versions can lead to unexpected behavior or errors.

## Sample Connection Code

Here's an example of how to connect to a 389 Directory Server instance using `lib389`, with configuration from environment variables:

```python
from lib389 import DirSrv
from lib389.idm.user import UserAccounts


def get_ldap_config():
    """Get LDAP configuration from environment variables."""
    return {
        'ldap_url': 'ldap://localhost:389',
        'base_dn': 'dc=example,dc=com',
        'bind_dn': 'cn=Directory Manager',
        'bind_password': 'Password123'
    }


def get_ldap_connection():
    """Create and return a connection to the LDAP server."""

    config = get_ldap_config()
    ds = DirSrv(verbose=True)
    try:
        ds.remote_simple_allocate(
            config['ldap_url'],
            config['bind_dn'],
            config['bind_password']
        )

        ds.open()
        print(f"Successfully connected to {config['ldap_url']} as {config['bind_dn']}")
        return ds

    except Exception as e:
        print(f"Failed to connect to LDAP server: {e}")
        raise


def search_users_example(basedn=None):
    """Example function to search for users and return their details as JSON."""

    config = get_ldap_config()
    search_basedn = basedn or config['base_dn']

    connection = get_ldap_connection()
    if not connection:
        print("Could not establish LDAP connection.")
        return []

    users = UserAccounts(connection, search_basedn)
    for user in users.list():
        print(user.display())

    print("Closing LDAP connection in search_users_example.")
    connection.unbind_s()


if __name__ == '__main__':
    search_users_example()
```

For more detailed examples on managing users, groups, services, and other directory features, please refer to the documentation within the `src/lib389/doc/source/` directory, such as `user.rst`, `group.rst`, etc.

## Contributing

Please see our [contributing guide](https://www.port389.org/docs/389ds/contributing.html).

## License

The 389 Directory Server is subject to the terms detailed in the
license agreement file called LICENSE in the main project directory.



