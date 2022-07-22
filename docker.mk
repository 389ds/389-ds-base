
suse:
	docker build -t 389-ds-suse:latest -f docker/389-ds-suse/Dockerfile .

fedora:
	docker build -t 389-ds-fedora:latest -f docker/389-ds-fedora/Dockerfile .
