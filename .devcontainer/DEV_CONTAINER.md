# 389 Directory Server Development Container

## What is this for?
The Development Container simplifies building and running the 389 Directory Server from anywhere including Windows, Mac, and Linux by leveraging a container. The environment is therefore already setup with the correct tools and tool versions to build and run.

## How does it work
You need to have a local container runtime such as [Docker Desktop](https://www.docker.com/products/docker-desktop/), [Podman](https://podman.io/), or [Kubernetes](https://kubernetes.io/). [Docker Compose](https://docs.docker.com/compose/) is recommended as well to simplify commands, and avoid long repetitive command lines, but not strictly required. If you don't have Docker Compose you will need to examine the `dev.yaml` and translate to container run commands. Container run commands with Kubernetes also [requires translation](https://kubernetes.io/docs/reference/kubectl/docker-cli-to-kubectl/).

By default, the project directory is bind mounted into the container such that changes can be made on the local workstation using your favorite IDE.  Other modes of operation are possible such as editing in-container or using remote development tooling such as [VS Code](https://code.visualstudio.com/docs/remote/remote-overview) or [IntelliJ](https://www.jetbrains.com/remote-development/).

## Get and start development container:
```
git clone https://github.com/389ds/389-ds-base
cd 389-ds-base/.devcontainer
docker compose up
```

## Build and Run inside the container:
```
docker exec -it bash dirsrv
cd 389-ds-base
./build.sh
/usr/libexec/dirsrv/dscontainer -r &
# Or run dsctl, etc.
```

## Configure:
You can configure the development container using environment variables.  Create a [.devcontainer/overrides.env](https://docs.docker.com/compose/environment-variables/set-environment-variables/) file in the project root.

Container Runtime Environment Variables:

| Name               | Description                                                                                                                                                       | Default  | 
|--------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------|
| CC                 | C compiler                                                                                                                                                        | gcc      |
| CXX                | C++ compiler                                                                                                                                                      | g++      |
| CFLAGS             | C compiler flags                                                                                                                                                  | "-O2 -g" |
| CXXFLAGS           | C++ compiler flags                                                                                                                                                |          |
| LDFLAGS            | Linker flags                                                                                                                                                      |          |
| DS_BACKEND_NAME    | Database backend                                                                                                                                                  | example  |
| DS_DM_PASSWORD     | Directory Manager password                                                                                                                                        | password |
| DS_START_TIMEOUT   | Number of seconds to start dscontainer before timeout                                                                                                             | 60       |
| DS_STOP_TIMEOUT    | Number of seconds to stop dscontainer before timeout                                                                                                              | 60       |
| CUSTOM_CRT_URL     | Optional URL to site-specific (self-signed) corporate intercepting TLS proxy CA file; sometimes necessary to allow build to download artifacts from the Internet. |          |
