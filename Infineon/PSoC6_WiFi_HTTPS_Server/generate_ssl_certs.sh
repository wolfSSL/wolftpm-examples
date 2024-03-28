#!/bin/sh

################################
# Become a Certificate Authority
################################

MY_DOMAIN_NAME=mysecurehttpserver.local

_OS=$(uname -s)
echo "Environment: $_OS"

if [[ "$_OS" == "MINGW"* ]]; then
    OPENSSL_SUBJECT_INFO="//C=US\ST=Montana\L=Bozeman\O=CY\OU=Engineering\CN=$MY_DOMAIN_NAME"
else
    OPENSSL_SUBJECT_INFO="/C=US/ST=Montana/L=Bozeman/O=CY/OU=Engineering/CN=$MY_DOMAIN_NAME"
fi

# Generate a private root key
openssl ecparam -name prime256v1 -genkey -noout -out root_ca.key

# Self-sign a certificate. Make sure to set the "Common Name" field to match
# your server name (HTTPS_SERVER_NAME) defined in the application.
openssl req -new -x509 -sha256 -key root_ca.key -out root_ca.crt -subj $OPENSSL_SUBJECT_INFO

########################
# Create CA-signed certs
########################

# Generate a private key
openssl ecparam -name prime256v1 -genkey -noout -out $MY_DOMAIN_NAME.key

# Create the Certificate Signing Request (CSR).
# Make sure to set the "Common Name" field with MY_DOMAIN_NAME.
openssl req -new -sha256 -key $MY_DOMAIN_NAME.key -out $MY_DOMAIN_NAME.csr \
-subj $OPENSSL_SUBJECT_INFO

# Create a config file for the extensions
>$MY_DOMAIN_NAME.ext cat <<-EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names
[alt_names]
DNS.1 = $MY_DOMAIN_NAME
EOF

# Create the signed certificate
openssl x509 -req -in $MY_DOMAIN_NAME.csr -CA root_ca.crt -CAkey root_ca.key -CAcreateserial -out $MY_DOMAIN_NAME.crt -days 1000 -sha256

################################
# Generate Client Certificate
################################
MY_CLIENT=mysecurehttpclient

# Generating RSA Private Key for Client Certificate
openssl ecparam -name prime256v1 -genkey -noout -out $MY_CLIENT.key

# Generating Certificate Signing Request for Client Certificate
openssl req -new -sha256 -key $MY_CLIENT.key -out $MY_CLIENT.csr \
-subj $OPENSSL_SUBJECT_INFO

# Generating Certificate for Client Certificate
openssl x509 -req -in $MY_CLIENT.csr -CA root_ca.crt -CAkey root_ca.key -CAcreateserial -out $MY_CLIENT.crt -days 1000 -sha256

# Bundle the client certificate and key.
# Export password is set to empty.
openssl pkcs12 -export -out $MY_CLIENT.pfx -inkey $MY_CLIENT.key -in $MY_CLIENT.crt \
-passout pass:

# We have now successfully generated the server and client certificates.
# Configure your server with the generated certificate, key, and rootCA.
# Configure your client by importing the generated PKCS12 file that
# bundles the client certificate and key.
#
# Remove the intermediate files.
rm rootCA.srl $MY_DOMAIN_NAME.csr $MY_DOMAIN_NAME.ext $MY_CLIENT.csr

echo "Done"
