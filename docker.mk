
suse:
	docker build -t 389-ds-suse:master -f docker/389-ds-suse/Dockerfile .

fedora:
	docker build -t 389-ds-fedora:master -f docker/389-ds-fedora/Dockerfile .
