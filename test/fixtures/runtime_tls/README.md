# Controlled TLS fixtures

These certificate materials are deterministic test-only fixtures generated for
the `localhost` TLS oracle. The encoded key is an unencrypted PKCS#8 DER value,
not a production credential. The Python harness reconstructs a temporary PEM
file only for its isolated loopback server and deletes it on exit.

This directory is test data and must remain excluded from every production,
installed, and loadable source inventory.
