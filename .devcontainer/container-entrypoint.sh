#!/bin/bash

##
# Check if a site-specific (self-signed) CA cert is set in the environment variable CUSTOM_CRT_URL,
# and if it is then update trust anchors.  This is necessary for users behind a corporate TLS
# intercepting proxy as otherwise the build will fail to download artifacts from the Internet.
#
# Note: check only runs if hasn't run yet.  However, if container isn't running as root then the
# location of marker files needs to be updated.
##
if [ ! -f /cert-check-complete ]; then
  if [ -z "$CUSTOM_CRT_URL" ] ; then echo "No custom cert specified in CUSTOM_CRT_URL env; skipping"; else \
    curl -o /etc/pki/ca-trust/source/anchors/customcert.crt $CUSTOM_CRT_URL
    update-ca-trust
  fi
  touch /cert-check-complete
else
  echo "Cert check already complete; skipping"
fi

echo "Container ready!"

##
# Note: PID 1 should actually be tini or similar.  If users decide to override the entrypoint
# it is recommended to continue to use tini or similar.
##
sleep infinity