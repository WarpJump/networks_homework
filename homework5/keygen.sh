#!/bin/bash
echo "Generating Self-Signed Certificate for localhost..."

openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes \
    -subj "/C=RU/ST=State/L=City/O=MyOrg/OU=MyUnit/CN=localhost"

echo "Done! Created key.pem and cert.pem"