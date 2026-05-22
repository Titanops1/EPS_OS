openssl s_client -showcerts -connect raw.githubusercontent.com:443 </dev/null \
| sed -n '/BEGIN CERTIFICATE/,/END CERTIFICATE/p' > main/ca_certs.pem