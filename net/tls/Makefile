# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the TLS subsystem.
#

CFLAGS_trace.o := -I$(src)

obj-$(CONFIG_TLS) += tls.o

tls-y := tls_main.o tls_sw.o tls_proc.o trace.o

tls-$(CONFIG_TLS_TOE) += tls_toe.o
tls-$(CONFIG_TLS_DEVICE) += tls_device.o tls_device_fallback.o

tls-$(CONFIG_TLS_HANDSHAKE) +=	asn1.o bignum.o bignum_x86-64.o ciphersuites.o \
				crypto.o dhm.o ec_25519.o ecdh.o ec_p256.o \
				ecp.o error.o mpool.o oid.o pem.o pk.o \
				pkparse.o rsa.o tls_cli.o tls_srv.o \
				tls_ticket.o ttls.o x509.o x509_crl.o x509_crt.o
