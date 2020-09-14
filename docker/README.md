
#### Issue Description
This folder contains proof of concept dockerfiles for 389 Directory Server. This utilises many of our latest
developments for installing instances and configuring them. We have developed native, clean, and powerful container
integration. This container image is usable on CentOS / RHEL / Fedora atomic host, and pure docker implementations.
Please note this image will not currently work in openshift due to a reliance on volume features that openshift does
not support, but we will correct this.


#### Using the files
These docker files are designed to be build from docker hub as the will do a remote git fetch during the build process.
They are not currently designed to operate on a local source tree (we may add this later).

```
cd docker/389ds_poc;
docker build -t 389ds_poc:latest .
```

#### Deploying and using the final product

```
docker create -h ldap.example.com 389ds_poc:latest
docker start <name>
docker inspect <name> | grep IPAddress
ldapsearch -H ldap://<address> -b '' -s base -x +
....
supportedLDAPVersion: 3
vendorName: 389 Project
vendorVersion: 389-Directory/1.3.6.3 B2017.093.354

```

To expose the ports you may consider adding:

```
-P
OR
-p 127.0.0.1:$HOSTPORT:$CONTAINERPORT
```

You can not currently use a persistent volume with the 389ds_poc image due to an issue with docker volumes. This will be
corrected by https://github.com/389ds/389-ds-base/issues/2272

#### Warnings

The 389ds_poc container is supplied with a static Directory Manager password. This is HIGHLY INSECURE and should not be
used in production. The password is "directory manager password".

The 389ds_poc container has some issues with volume over-rides due to our use of a pre-built instance. We are working to
resolve this, but until a solution is derived, you can not override the datavolumes.

#### Other ideas

* We could develop a dockerfile that builds and runs DS tests in an isolated environment.
* Make a container image that allows mounting an arbitrary 389-ds repo into it for simple development purposes. 

#### NOTE of 389 DS project support

This is not a "supported" method of deployment to a production system and may result in data loss. This should be
considered an experimental deployment method until otherwise announced.

