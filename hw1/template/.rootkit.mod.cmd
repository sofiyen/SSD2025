cmd_/home/sofiyen/SSD2025/hw1/template/rootkit.mod := printf '%s\n'   rootkit.o | awk '!x[$$0]++ { print("/home/sofiyen/SSD2025/hw1/template/"$$0) }' > /home/sofiyen/SSD2025/hw1/template/rootkit.mod
